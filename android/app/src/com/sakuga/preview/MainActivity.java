package com.sakuga.preview;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Bundle;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.FrameLayout;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.IOException;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.HashSet;
import java.util.Arrays;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;

/** Shared Android Vulkan sandbox, with a separate Canvas animation fallback. */
public final class MainActivity extends Activity {
    static { System.loadLibrary("sakuga_mobile"); }
    private static native long nativeCreate(String[] paths, String cacheDir);
    private static native void nativeDestroy(long handle);
    private static native void nativeUpdate(long handle, float dt, float x, float y);
    // Actions: 0 down, 1 move, 2 up, 3 cancel (kept independent of Android's integer constants).
    private static native float[] nativeTouch(long handle, int action, int pointerId, float x, float y,
                                               float width, float height, float density);
    private static native float[] nativeJoints(long handle);
    private static native String nativeInfo(long handle);
    private static native int nativeClipIndex(long handle);
    private static native boolean nativeSelectClip(long handle, int index);
    private static native String nativeProbeSurface(Surface surface);
    private static native long nativeRendererCreate(Surface surface, String shaderDir, String modelPath, String[] motionPaths, float density);
    private static native int nativeRendererFrame(long renderer, float x, float y, int clip);
    private static native void nativeRendererPause(long renderer);
    private static native void nativeRendererResume(long renderer);
    private static native void nativeRendererTouch(long renderer, boolean imgui, int action, int id, float x, float y);
    private static native String nativeRendererError();
    private static native void nativeRendererDestroy(long renderer);
    private static final Object ASSET_LOCK = new Object();
    private volatile boolean destroyed, resumed;
    private volatile int surfaceGeneration;
    private volatile boolean surfaceLive;
    private Thread surfaceThread;
    private volatile String[] motionPaths;
    private volatile String modelPath;
    private volatile boolean assetsFailed;
    private volatile float renderMoveX, renderMoveY;
    private volatile int renderClip = -1;
    private static final class UiTouch {
        final boolean imgui; final int action, id; final float x, y;
        UiTouch(boolean imgui, int action, int id, float x, float y) { this.imgui = imgui; this.action = action; this.id = id; this.x = x; this.y = y; }
    }
    private final ConcurrentLinkedQueue<UiTouch> uiTouches = new ConcurrentLinkedQueue<>();
    private String shaderDir;
    private final Object surfaceLock = new Object();
    // Serialize fallback creation, use and destruction across Activity recreation.
    // The Activity is singleTask; no fallback database is kept while its Vulkan scene is alive.
    private static final ReentrantLock fallbackLock = new ReentrantLock();
    private static long fallbackHandle;
    private static MainActivity fallbackOwner;
    private boolean forceCanvas;
    private PreviewView preview;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        // Keep system bars from covering the editor and touch controls; a swipe can reveal them temporarily.
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        forceCanvas = getIntent().getBooleanExtra("sakuga_force_canvas", false);
        preview = new PreviewView();
        try {
            File dir = new File(getFilesDir(), "shaders");
            if (!dir.exists() && !dir.mkdirs()) throw new IOException("Cannot create shader directory");
            String[] shaders = getAssets().list("shaders");
            if (shaders == null || shaders.length == 0) throw new IOException("Shaders missing from APK");
            for (String name : shaders) {
                try (InputStream in = getAssets().open("shaders/" + name); FileOutputStream out = new FileOutputStream(new File(dir, name))) {
                    byte[] buffer = new byte[32768]; int n; while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
                }
            }
            shaderDir = dir.getAbsolutePath();
        } catch (IOException e) { shaderDir = null; preview.setSurfaceStatus("Shader setup failed: " + e.getMessage()); }
        // The SurfaceView is underneath the touch overlay and the fallback Canvas preview.
        FrameLayout layout = new FrameLayout(this);
        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) {
                final int generation;
                synchronized (surfaceLock) { surfaceLive = true; generation = ++surfaceGeneration; }
                final Surface surface = holder.getSurface();
                preview.setSurfaceStatus("Checking Android Vulkan surface...");
                surfaceThread = new Thread(new Runnable() { @Override public void run() {
                    long renderer = 0;
                    String fallbackReason = null;
                    try {
                        final String status;
                        synchronized (surfaceLock) {
                            if (!surfaceLive || generation != surfaceGeneration) return;
                            status = surface.isValid() ? nativeProbeSurface(surface) : "Android surface unavailable";
                        }
                        while (surfaceLive && !destroyed && motionPaths == null && !assetsFailed) {
                            try { Thread.sleep(50); } catch (InterruptedException e) { Thread.currentThread().interrupt(); return; }
                        }
                        if (assetsFailed || motionPaths == null) return;
                        if (forceCanvas) { fallbackReason = "Canvas preview requested"; return; }
                        // An old fallback may still be building on another worker. Wait without blocking
                        // the UI thread and check surface generation so surfaceDestroyed can join promptly.
                        boolean locked = false;
                        while (surfaceLive && !destroyed && generation == surfaceGeneration && !locked) {
                            try { locked = fallbackLock.tryLock(100, TimeUnit.MILLISECONDS); }
                            catch (InterruptedException e) { Thread.currentThread().interrupt(); return; }
                        }
                        if (!locked) return;
                        try {
                            if (!surfaceLive || destroyed || generation != surfaceGeneration) return;
                            preview.releaseFallbackLocked();
                        } finally { fallbackLock.unlock(); }
                        synchronized (surfaceLock) {
                            if (!surfaceLive || generation != surfaceGeneration) return;
                            if (surface.isValid() && shaderDir != null && modelPath != null)
                                renderer = nativeRendererCreate(surface, shaderDir, modelPath, motionPaths, getResources().getDisplayMetrics().density);
                        }
                        if (renderer == 0) {
                            String failure = shaderDir == null ? "Shaders unavailable" : nativeRendererError();
                            fallbackReason = failure.isEmpty() ? status : failure;
                            return;
                        }
                        boolean firstFrame = true, wasPaused = false;
                        while (surfaceLive && !destroyed) {
                            if (!resumed) {
                                if (!wasPaused) {
                                    synchronized (surfaceLock) { uiTouches.clear(); nativeRendererPause(renderer); }
                                    wasPaused = true;
                                }
                                try { Thread.sleep(50); } catch (InterruptedException e) { Thread.currentThread().interrupt(); break; }
                                continue;
                            }
                            if (wasPaused) { nativeRendererResume(renderer); wasPaused = false; }
                            int frameResult;
                            synchronized (surfaceLock) {
                                if (!surfaceLive || generation != surfaceGeneration) break;
                                UiTouch input;
                                HashSet<Integer> updatedGamePointers = new HashSet<>();
                                while ((input = uiTouches.peek()) != null) {
                                    // Never collapse a complete press/move/release into one frame: action buttons and
                                    // short swipes must be observed by the engine for at least one simulation step.
                                    if (!input.imgui && input.action == 2 && updatedGamePointers.contains(input.id)) break;
                                    uiTouches.poll();
                                    nativeRendererTouch(renderer, input.imgui, input.action, input.id, input.x, input.y);
                                    if (!input.imgui && input.action != 3) updatedGamePointers.add(input.id);
                                }
                                frameResult = nativeRendererFrame(renderer, renderMoveX, renderMoveY, renderClip);
                            }
                            if (frameResult < 0) {
                                final String failure = nativeRendererError();
                                fallbackReason = failure;
                                android.util.Log.e("SakugaPreview", "renderer frame failed: " + failure);
                                runOnUiThread(new Runnable() { @Override public void run() {
                                    if (!destroyed && generation == surfaceGeneration) {
                                        preview.setVulkanActive(false); preview.setSurfaceStatus("Canvas fallback: " + failure);
                                    }
                                }});
                                break;
                            }
                            if (firstFrame && frameResult > 0) {
                                firstFrame = false;
                                android.util.Log.i("SakugaPreview", "engine renderer first frame presented");
                                runOnUiThread(new Runnable() { @Override public void run() {
                                    if (!destroyed && generation == surfaceGeneration) {
                                        preview.setVulkanActive(true); preview.setSurfaceStatus("Engine sandbox • bundled VRM • 77 motion clips");
                                    }
                                }});
                            }
                            // FIFO presentation provides display pacing; an additional fixed sleep
                            // after each frame adds to render time and can miss a refresh.
                            // Avoid spinning when no frame was presented (e.g. surface resize).
                            if (frameResult == 0) {
                                try { Thread.sleep(8); } catch (InterruptedException e) { Thread.currentThread().interrupt(); break; }
                            }
                        }
                    } finally {
                        if (renderer != 0) nativeRendererDestroy(renderer);
                        // Do not build a second full database until all Vulkan resources are released.
                        if (fallbackReason != null && surfaceLive && !destroyed && generation == surfaceGeneration)
                            startCanvasFallback(generation, fallbackReason);
                    }
                }}, "vulkan-renderer");
                surfaceThread.start();
            }
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { }
            @Override public void surfaceDestroyed(SurfaceHolder holder) {
                synchronized (surfaceLock) { surfaceLive = false; ++surfaceGeneration; }
                // Do not return while Vulkan still owns this surface or native window.
                if (surfaceThread != null) {
                    boolean interrupted = false;
                    for (;;) {
                        try { surfaceThread.join(); break; } catch (InterruptedException e) { interrupted = true; }
                    }
                    if (interrupted) Thread.currentThread().interrupt();
                    surfaceThread = null;
                }
                uiTouches.clear();
                if (!destroyed) { preview.setVulkanActive(false); preview.setSurfaceStatus("Android surface inactive"); }
            }
        });
        layout.addView(surfaceView, new FrameLayout.LayoutParams(-1, -1));
        layout.addView(preview, new FrameLayout.LayoutParams(-1, -1));
        setContentView(layout);
        new Thread(new Runnable() { @Override public void run() {
            String error = null;
            try {
                File root = new File(getFilesDir(), "sandbox");
                File dir = new File(root, "assets/motion");
                // APK updates kill the old process. Avoid rewriting assets on Activity recreation while
                // another surface may still be reading them. The marker is written only after success.
                synchronized (ASSET_LOCK) {
                    if (!root.exists() && !root.mkdirs()) throw new IOException("Cannot create asset root");
                    long version = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
                    File marker = new File(root, ".assets-" + version);
                    if (!marker.isFile()) {
                        File[] entries = root.listFiles();
                        if (entries == null) throw new IOException("Cannot list asset root");
                        for (File entry : entries) if (entry.getName().startsWith(".assets-") && !entry.delete())
                            throw new IOException("Cannot remove old asset marker");
                        deleteAssetDirectory(dir);
                        copyAssetDirectory("motion", dir);
                        copyAssetDirectory("models", new File(root, "assets/models"));
                        copyAssetDirectory("luts", new File(root, "assets/luts"));
                        copyAssetDirectory("env", new File(root, "assets/env"));
                        copyAssetDirectory("config", new File(root, "config"));
                        copyAssetDirectory("licenses", new File(root, "assets/licenses"));
                        if (!marker.createNewFile()) throw new IOException("Cannot mark assets ready");
                    }
                }
                File[] clips = dir.listFiles();
                if (clips == null) throw new IOException("Cannot list motion clips");
                Arrays.sort(clips);
                java.util.ArrayList<String> paths = new java.util.ArrayList<>();
                for (File clip : clips) if (clip.isFile() && clip.getName().endsWith(".bvh")) paths.add(clip.getAbsolutePath());
                if (paths.size() != 77) throw new IOException("Expected 77 motion clips, found " + paths.size());
                File model = new File(root, "assets/models/AvatarSample.vrm");
                modelPath = model.getAbsolutePath(); motionPaths = paths.toArray(new String[0]);
                android.util.Log.i("SakugaPreview", "motion assets ready clips=" + paths.size());
            } catch (Exception e) { error = "Could not start preview: " + e.getMessage(); assetsFailed = true; }
            final String message = error;
            runOnUiThread(new Runnable() { @Override public void run() {
                if (!destroyed && message != null) preview.setMessage(message);
            }});
        }}, "motion-loader").start();
    }
    private void startCanvasFallback(final int generation, final String reason) {
        runOnUiThread(new Runnable() { @Override public void run() {
            if (!destroyed && surfaceLive && generation == surfaceGeneration) {
                preview.setVulkanActive(false);
                preview.setSurfaceStatus("Canvas fallback: " + reason);
                preview.setMessage("Preparing all 77 animations for Canvas fallback...");
            }
        }});
        new Thread(new Runnable() { @Override public void run() {
            long h = 0;
            fallbackLock.lock();
            try {
                if (destroyed || !surfaceLive || generation != surfaceGeneration || motionPaths == null) return;
                if (preview.hasFallbackLocked()) {
                    h = fallbackHandle;
                } else {
                    preview.releaseFallbackLocked();
                    File cache = new File(getFilesDir(), "sandbox/user/mm_cache");
                    h = nativeCreate(motionPaths, cache.getAbsolutePath());
                    if (destroyed || !surfaceLive || generation != surfaceGeneration) {
                        if (h != 0) nativeDestroy(h);
                        return;
                    }
                    if (h != 0) preview.installFallbackLocked(h);
                }
            } finally { fallbackLock.unlock(); }
            final boolean ready = h != 0;
            runOnUiThread(new Runnable() { @Override public void run() {
                if (!destroyed && surfaceLive && generation == surfaceGeneration) {
                    preview.setMessage(ready ? null : "Canvas fallback could not load all 77 animations.");
                    if (ready) android.util.Log.i("SakugaPreview", "canvas fallback ready clips=77");
                }
            }});
        }}, "canvas-motion-loader").start();
    }
    private void deleteAssetDirectory(File dir) throws IOException {
        if (!dir.exists()) return;
        File[] children = dir.listFiles();
        if (children == null) throw new IOException("Cannot list asset directory " + dir);
        for (File child : children) {
            if (child.isDirectory()) deleteAssetDirectory(child);
            else if (!child.delete()) throw new IOException("Cannot remove stale asset " + child);
        }
        if (!dir.delete()) throw new IOException("Cannot remove asset directory " + dir);
    }
    private void copyAssetDirectory(String assetDir, File dest) throws IOException {
        if (!dest.exists() && !dest.mkdirs()) throw new IOException("Cannot create asset directory " + dest);
        String[] names = getAssets().list(assetDir);
        if (names == null) throw new IOException("Missing assets: " + assetDir);
        for (String name : names) {
            String path = assetDir + "/" + name;
            String[] children = getAssets().list(path);
            if (children != null && children.length > 0) copyAssetDirectory(path, new File(dest, name));
            else try (InputStream in = getAssets().open(path); FileOutputStream out = new FileOutputStream(new File(dest, name))) {
                byte[] buffer = new byte[32768]; int n; while ((n = in.read(buffer)) != -1) out.write(buffer, 0, n);
            }
        }
    }
    @Override protected void onResume() { super.onResume(); resumed = true; if (preview != null) preview.setRunning(true); }
    @Override protected void onPause() { resumed = false; if (preview != null) preview.setRunning(false); super.onPause(); }
    @Override protected void onDestroy() { destroyed = true; if (preview != null) preview.close(); super.onDestroy(); }

    private final class PreviewView extends View {
        private final android.text.TextPaint paint = new android.text.TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final int[][] bones = {{0,1},{1,2},{2,3},{3,4},{4,5},{3,6},{6,7},{7,8},{8,9},
                {3,10},{10,11},{11,12},{12,13},{0,14},{14,15},{15,16},{16,17},{0,18},{18,19},{19,20},{20,21}};
        private final Bitmap base, knob;
        private long lastNs; private boolean running;
        private String message = "Preparing Android sandbox and 77 animations...";
        private String surfaceStatus = "Waiting for Android surface...";
        private boolean vulkanActive, loggedClipPicker;
        private float stickX, stickY;
        private final HashSet<Integer> gamePointers = new HashSet<>();
        private final float density = getResources().getDisplayMetrics().density;
        PreviewView() {
            super(MainActivity.this); setBackgroundColor(Color.rgb(11,14,20));
            base = image("controls/joystick_base.png"); knob = image("controls/joystick_knob.png");
        }
        private Bitmap image(String name) {
            try (InputStream in = getAssets().open(name)) { return BitmapFactory.decodeStream(in); }
            catch (IOException e) { return null; }
        }
        // Caller holds fallbackLock; the Canvas UI only uses the handle while holding the same lock.
        boolean hasFallbackLocked() { return fallbackHandle != 0 && fallbackOwner == MainActivity.this; }
        void installFallbackLocked(long h) { fallbackHandle = h; fallbackOwner = MainActivity.this; }
        void releaseFallbackLocked() {
            if (fallbackHandle != 0) { long old = fallbackHandle; fallbackHandle = 0; fallbackOwner = null; nativeDestroy(old); }
        }
        void releaseOwnedFallbackLocked() { if (fallbackOwner == MainActivity.this) releaseFallbackLocked(); }
        void setMessage(String text) { message = text; lastNs = 0; invalidate(); }
        void setSurfaceStatus(String status) { surfaceStatus = status; invalidate(); }
        void setVulkanActive(boolean active) {
            if (!active) cancelAllTouches();
            vulkanActive = active; setBackgroundColor(active ? Color.TRANSPARENT : Color.rgb(11,14,20)); invalidate();
        }
        void setRunning(boolean enabled) {
            running = enabled; lastNs = 0;
            if (!enabled) { touch(3, -1, 0, 0); stickX = stickY = 0; cancelAllTouches(); }
            invalidate();
        }
        void close() {
            running = false;
            // The loader checks destroyed before publishing its result, so do not block the UI
            // if it is currently building the database.
            if (fallbackLock.tryLock()) { try { releaseOwnedFallbackLocked(); } finally { fallbackLock.unlock(); } }
            else new Thread(new Runnable() { @Override public void run() {
                fallbackLock.lock();
                try { releaseOwnedFallbackLocked(); } finally { fallbackLock.unlock(); }
            }}, "canvas-cleanup").start();
        }
        private void text(Canvas c, String value, float x, float y, float size, int color) {
            paint.setStyle(Paint.Style.FILL); paint.setColor(color); paint.setTextSize(size * density); c.drawText(value, x, y, paint);
        }
        private void showClipPicker() {
            final String[] paths = motionPaths;
            if (paths == null) return;
            String[] names = new String[paths.length];
            for (int i = 0; i < paths.length; ++i) {
                String name = new File(paths[i]).getName();
                names[i] = name.endsWith(".bvh") ? name.substring(0, name.length() - 4) : name;
            }
            new android.app.AlertDialog.Builder(MainActivity.this).setTitle("Animations (77)")
                .setItems(names, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface dialog, int index) {
                        if (!fallbackLock.tryLock()) return;
                        try { if (hasFallbackLocked()) nativeSelectClip(fallbackHandle, index); }
                        finally { fallbackLock.unlock(); }
                        invalidate();
                    }
                }).setNegativeButton("Close", null).show();
        }
        @Override protected void onDraw(Canvas c) {
            super.onDraw(c);
            // The Vulkan sandbox draws its own ImGui and virtual controls. The Canvas view is only
            // an input overlay in that mode; drawing fallback labels here obscures the editor.
            if (vulkanActive) return;
            int w = getWidth(), h = getHeight();
            float margin = 18 * density;
            text(c, "SAKUGA  /  ANIMATION PREVIEW", margin, 28 * density, 17, Color.rgb(184,235,250));
            text(c, "77 motion clips  •  synthetic rig  •  Canvas fallback", margin, 48 * density, 11, Color.LTGRAY);
            paint.setColor(Color.rgb(33,60,78)); paint.setStyle(Paint.Style.FILL);
            c.drawRoundRect(new RectF(w - margin - 90*density, margin, w - margin, margin + 38*density), 10*density, 10*density, paint);
            text(c, "CLIPS", w - margin - 70*density, margin + 25*density, 12, Color.WHITE);
            if (!fallbackLock.tryLock()) { text(c, message == null ? "Loading..." : message, margin, h * .5f, 14, Color.WHITE); return; }
            try {
            if (!hasFallbackLocked()) { text(c, message == null ? "Loading..." : message, margin, h * .5f, 14, Color.WHITE); return; }
            if (!loggedClipPicker) {
                android.util.Log.i("SakugaPreview", "canvas picker target x=" + (int)(w-margin-45*density) + " y=" + (int)(margin+19*density));
                loggedClipPicker = true;
            }
            long handle = fallbackHandle;
            long now = System.nanoTime(); float dt = lastNs == 0 ? 0 : (now - lastNs) / 1000000000f; lastNs = now;
            if (running && !vulkanActive) nativeUpdate(handle, dt, stickX, -stickY);
            float[] j = nativeJoints(handle);
            renderClip = nativeClipIndex(handle);
            if (!vulkanActive && j != null && j.length == 66) {
                float scale = Math.min(h * .38f, w * .20f), centerX = w * .5f, bottomY = h * .85f;
                paint.setColor(Color.rgb(86,207,241)); paint.setStrokeWidth(Math.max(3, 2.5f * density)); paint.setStyle(Paint.Style.STROKE);
                for (int[] b : bones) c.drawLine(centerX + (j[b[0]*3] + j[b[0]*3+2] * .35f)*scale, bottomY-j[b[0]*3+1]*scale,
                        centerX + (j[b[1]*3] + j[b[1]*3+2] * .35f)*scale, bottomY-j[b[1]*3+1]*scale, paint);
                paint.setColor(Color.WHITE); paint.setStyle(Paint.Style.FILL);
                for (int i = 0; i < 22; ++i) c.drawCircle(centerX+(j[i*3]+j[i*3+2]*.35f)*scale, bottomY-j[i*3+1]*scale, Math.max(2,2*density), paint);
            }
            text(c, vulkanActive ? "Motion lab  •  virtual touch controls" : nativeInfo(handle), margin, 70 * density, 12, Color.WHITE);
            paint.setTextSize(10 * density);
            CharSequence visibleStatus = android.text.TextUtils.ellipsize(surfaceStatus, paint, w - margin * 2,
                    android.text.TextUtils.TruncateAt.END);
            text(c, visibleStatus.toString(), margin, 89 * density, 10, Color.LTGRAY);
            if (!vulkanActive) {
            float r = Math.min(w,h)*.13f, bx = margin+r, by = h-margin-r;
            bitmap(c, base, bx, by, r); bitmap(c, knob, bx+stickX*r*.55f, by+stickY*r*.55f, r*.5f);
            paint.setColor(Color.rgb(33,60,78)); paint.setStyle(Paint.Style.FILL);
            c.drawRoundRect(new RectF(w-margin-r*2, h-margin-r*1.4f, w-margin, h-margin), 14*density,14*density,paint);
            text(c, "NEXT CLIP", w-margin-r*1.8f, h-margin-r*.6f, 12, Color.WHITE);
            text(c, "Drag stick to match • tap NEXT to browse", margin, h - 4*density, 10, Color.LTGRAY);
            }
            } finally { fallbackLock.unlock(); }
            if (running) postInvalidateOnAnimation();
        }
        private void bitmap(Canvas c, Bitmap img, float x, float y, float r) {
            if (img == null) return;
            paint.setColor(Color.WHITE); paint.setAlpha(220); c.drawBitmap(img, null, new RectF(x-r,y-r,x+r,y+r), paint); paint.setAlpha(255);
        }
        private void touch(int action, int id, float x, float y) {
            if (!fallbackLock.tryLock()) return;
            try {
                if (!hasFallbackLocked()) return;
                float[] position = nativeTouch(fallbackHandle, action, id, x, y, getWidth(), getHeight(), density);
                if (position != null && position.length == 2) { stickX = position[0]; stickY = position[1]; renderMoveX = stickX; renderMoveY = -stickY; }
            } finally { fallbackLock.unlock(); }
        }
        private void cancelAllTouches() {
            if (!gamePointers.isEmpty()) uiTouches.add(new UiTouch(false, 3, -1, 0, 0));
            gamePointers.clear();
        }
        @Override public boolean onTouchEvent(MotionEvent e) {
            int action = e.getActionMasked(), index = e.getActionIndex();
            if (!vulkanActive && action == MotionEvent.ACTION_DOWN &&
                    e.getX(index) >= getWidth() - 108*density && e.getX(index) <= getWidth() - 18*density &&
                    e.getY(index) >= 18*density && e.getY(index) <= 56*density) {
                if (fallbackLock.tryLock()) {
                    boolean ready;
                    try { ready = hasFallbackLocked(); } finally { fallbackLock.unlock(); }
                    if (ready) showClipPicker();
                }
                return true;
            }
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                int id = e.getPointerId(index); float x = e.getX(index), y = e.getY(index);
                if (vulkanActive) { gamePointers.add(id); uiTouches.add(new UiTouch(false, 0, id, x, y)); }
                else touch(0, id, x, y);
            } else if (action == MotionEvent.ACTION_MOVE) {
                for (int i = 0; i < e.getPointerCount(); ++i) {
                    int id = e.getPointerId(i); float x = e.getX(i), y = e.getY(i);
                    if (gamePointers.contains(id)) uiTouches.add(new UiTouch(false, 1, id, x, y));
                    else if (!vulkanActive) touch(1, id, x, y);
                }
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                int id = e.getPointerId(index); float x = e.getX(index), y = e.getY(index);
                if (gamePointers.remove(id)) {
                    uiTouches.add(new UiTouch(false, 1, id, x, y));
                    uiTouches.add(new UiTouch(false, 2, id, x, y));
                } else if (!vulkanActive) touch(2, id, x, y);
            } else if (action == MotionEvent.ACTION_CANCEL) { touch(3, -1, 0, 0); cancelAllTouches(); }
            invalidate(); return true;
        }
    }
}
