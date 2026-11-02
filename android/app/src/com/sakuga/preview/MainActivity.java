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

/** Experimental Android scene with a Canvas animation fallback. Not the complete desktop sandbox. */
public final class MainActivity extends Activity {
    static { System.loadLibrary("sakuga_mobile"); }
    private static native long nativeCreate(String[] paths);
    private static native void nativeDestroy(long handle);
    private static native void nativeUpdate(long handle, float dt, float x, float y);
    // Actions: 0 down, 1 move, 2 up, 3 cancel (kept independent of Android's integer constants).
    private static native float[] nativeTouch(long handle, int action, int pointerId, float x, float y,
                                               float width, float height, float density);
    private static native float[] nativeJoints(long handle);
    private static native String nativeInfo(long handle);
    private static native int nativeClipIndex(long handle);
    private static native String nativeProbeSurface(Surface surface);
    private static native long nativeRendererCreate(Surface surface, String shaderDir, String modelPath, String[] motionPaths, float density);
    private static native int nativeRendererFrame(long renderer, float x, float y, int clip);
    private static native void nativeRendererTouch(long renderer, int action, float x, float y);
    private static native String nativeRendererError();
    private static native void nativeRendererDestroy(long renderer);
    private volatile boolean destroyed;
    private int surfaceGeneration;
    private volatile boolean surfaceLive;
    private Thread surfaceThread;
    private volatile String[] motionPaths;
    private volatile String modelPath;
    private volatile boolean assetsFailed;
    private volatile float renderMoveX, renderMoveY;
    private volatile int renderClip = -1;
    private static final class UiTouch {
        final int action; final float x, y;
        UiTouch(int action, float x, float y) { this.action = action; this.x = x; this.y = y; }
    }
    private final ConcurrentLinkedQueue<UiTouch> uiTouches = new ConcurrentLinkedQueue<>();
    private String shaderDir;
    private final Object surfaceLock = new Object();
    private PreviewView preview;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
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
                    try {
                        final String status;
                        synchronized (surfaceLock) {
                            if (!surfaceLive || generation != surfaceGeneration) return;
                            status = surface.isValid() ? nativeProbeSurface(surface) : "Android surface unavailable";
                        }
                        while (surfaceLive && !destroyed && motionPaths == null && !assetsFailed) {
                            try { Thread.sleep(50); } catch (InterruptedException e) { Thread.currentThread().interrupt(); return; }
                        }
                        synchronized (surfaceLock) {
                            if (!surfaceLive || generation != surfaceGeneration) return;
                            if (surface.isValid() && shaderDir != null && motionPaths != null && modelPath != null)
                                renderer = nativeRendererCreate(surface, shaderDir, modelPath, motionPaths, getResources().getDisplayMetrics().density);
                        }
                        if (renderer == 0) {
                            final String failure = shaderDir == null ? "Shaders unavailable" : nativeRendererError();
                            runOnUiThread(new Runnable() { @Override public void run() {
                                if (!destroyed && generation == surfaceGeneration)
                                    preview.setSurfaceStatus("Canvas fallback: " + (failure.isEmpty() ? status : failure));
                            }});
                            return;
                        }
                        boolean firstFrame = true;
                        while (surfaceLive && !destroyed) {
                            int frameResult;
                            synchronized (surfaceLock) {
                                if (!surfaceLive || generation != surfaceGeneration) break;
                                UiTouch input;
                                while ((input = uiTouches.poll()) != null) nativeRendererTouch(renderer, input.action, input.x, input.y);
                                frameResult = nativeRendererFrame(renderer, renderMoveX, renderMoveY, renderClip);
                            }
                            if (frameResult < 0) {
                                final String failure = nativeRendererError();
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
                                        preview.setVulkanActive(true); preview.setSurfaceStatus("Engine Vulkan scene • bundled VRM • 2 motion clips");
                                    }
                                }});
                            }
                            try { Thread.sleep(16); } catch (InterruptedException e) { Thread.currentThread().interrupt(); break; }
                        }
                    } finally {
                        if (renderer != 0) nativeRendererDestroy(renderer);
                    }
                }}, "vulkan-renderer");
                surfaceThread.start();
            }
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { }
            @Override public void surfaceDestroyed(SurfaceHolder holder) {
                synchronized (surfaceLock) { surfaceLive = false; ++surfaceGeneration; }
                // Do not return while Vulkan still owns this surface or native window.
                if (surfaceThread != null) {
                    try { surfaceThread.join(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
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
            long handle = 0; String error = null;
            try {
                String[] names = {"walk1_subject1.bvh", "run1_subject2.bvh"};
                String[] paths = new String[names.length];
                File dir = new File(getFilesDir(), "motion");
                if (!dir.exists() && !dir.mkdirs()) throw new IOException("Cannot create motion directory");
                for (int i = 0; i < names.length; ++i) {
                    File out = new File(dir, names[i]);
                    // Replace from the bundled asset on every start so an app update cannot use stale clips.
                    try (InputStream in = getAssets().open("motion/" + names[i]); FileOutputStream dst = new FileOutputStream(out)) {
                        byte[] buffer = new byte[32768]; int n;
                        while ((n = in.read(buffer)) != -1) dst.write(buffer, 0, n);
                    }
                    paths[i] = out.getAbsolutePath();
                }
                File model = new File(getFilesDir(), "AvatarSample.vrm");
                try (InputStream in = getAssets().open("models/AvatarSample.vrm"); FileOutputStream dst = new FileOutputStream(model)) {
                    byte[] buffer = new byte[32768]; int n; while ((n = in.read(buffer)) != -1) dst.write(buffer, 0, n);
                }
                handle = nativeCreate(paths);
                if (handle == 0) error = "Could not load motion data. See Android logcat.";
                else { modelPath = model.getAbsolutePath(); motionPaths = paths; }
            } catch (Exception e) { error = "Could not start preview: " + e.getMessage(); }
            if (handle == 0) assetsFailed = true;
            final long result = handle; final String message = error;
            runOnUiThread(new Runnable() { @Override public void run() {
                if (destroyed) { if (result != 0) nativeDestroy(result); return; }
                preview.setSession(result, message);
            }});
        }}, "motion-loader").start();
    }
    @Override protected void onResume() { super.onResume(); if (preview != null) preview.setRunning(true); }
    @Override protected void onPause() { if (preview != null) preview.setRunning(false); super.onPause(); }
    @Override protected void onDestroy() { destroyed = true; if (preview != null) preview.close(); super.onDestroy(); }

    private final class PreviewView extends View {
        private final android.text.TextPaint paint = new android.text.TextPaint(Paint.ANTI_ALIAS_FLAG);
        private final int[][] bones = {{0,1},{1,2},{2,3},{3,4},{4,5},{3,6},{6,7},{7,8},{8,9},
                {3,10},{10,11},{11,12},{12,13},{0,14},{14,15},{15,16},{16,17},{0,18},{18,19},{19,20},{20,21}};
        private final Bitmap base, knob;
        private long handle; private long lastNs; private boolean running;
        private String message = "Loading two sample animations...";
        private String surfaceStatus = "Waiting for Android surface...";
        private boolean vulkanActive;
        private float stickX, stickY;
        private int uiPointer = -1;
        private final float density = getResources().getDisplayMetrics().density;
        PreviewView() {
            super(MainActivity.this); setBackgroundColor(Color.rgb(11,14,20));
            base = image("controls/joystick_base.png"); knob = image("controls/joystick_knob.png");
        }
        private Bitmap image(String name) {
            try (InputStream in = getAssets().open(name)) { return BitmapFactory.decodeStream(in); }
            catch (IOException e) { return null; }
        }
        void setSession(long h, String error) { handle = h; message = error; lastNs = 0; invalidate(); }
        void setSurfaceStatus(String status) { surfaceStatus = status; invalidate(); }
        void setVulkanActive(boolean active) {
            if (!active) cancelUiTouch();
            vulkanActive = active; setBackgroundColor(active ? Color.TRANSPARENT : Color.rgb(11,14,20)); invalidate();
        }
        void setRunning(boolean enabled) {
            running = enabled; lastNs = 0;
            if (!enabled) { touch(3, -1, 0, 0); stickX = stickY = 0; cancelUiTouch(); }
            invalidate();
        }
        void close() { running = false; if (handle != 0) { nativeDestroy(handle); handle = 0; } }
        private void text(Canvas c, String value, float x, float y, float size, int color) {
            paint.setStyle(Paint.Style.FILL); paint.setColor(color); paint.setTextSize(size * density); c.drawText(value, x, y, paint);
        }
        @Override protected void onDraw(Canvas c) {
            super.onDraw(c); int w = getWidth(), h = getHeight();
            float margin = 18 * density;
            text(c, "SAKUGA  /  ANIMATION PREVIEW", margin, 28 * density, 17, Color.rgb(184,235,250));
            text(c, vulkanActive ? "2 sample clips  •  bundled VRM  •  engine Vulkan scene" : "2 sample clips  •  synthetic rig  •  Canvas fallback", margin, 48 * density, 11, Color.LTGRAY);
            if (handle == 0) { text(c, message == null ? "Loading..." : message, margin, h * .5f, 14, Color.WHITE); return; }
            long now = System.nanoTime(); float dt = lastNs == 0 ? 0 : (now - lastNs) / 1000000000f; lastNs = now;
            if (running) nativeUpdate(handle, dt, stickX, -stickY);
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
            text(c, nativeInfo(handle), margin, 70 * density, 12, Color.WHITE);
            paint.setTextSize(10 * density);
            CharSequence visibleStatus = android.text.TextUtils.ellipsize(surfaceStatus, paint, w - margin * 2,
                    android.text.TextUtils.TruncateAt.END);
            text(c, visibleStatus.toString(), margin, 89 * density, 10, Color.LTGRAY);
            float r = Math.min(w,h)*.13f, bx = margin+r, by = h-margin-r;
            bitmap(c, base, bx, by, r); bitmap(c, knob, bx+stickX*r*.55f, by+stickY*r*.55f, r*.5f);
            paint.setColor(Color.rgb(33,60,78)); paint.setStyle(Paint.Style.FILL);
            c.drawRoundRect(new RectF(w-margin-r*2, h-margin-r*1.4f, w-margin, h-margin), 14*density,14*density,paint);
            text(c, "NEXT CLIP", w-margin-r*1.8f, h-margin-r*.6f, 12, Color.WHITE);
            text(c, "Drag stick to match • tap NEXT to browse", margin, h - 4*density, 10, Color.LTGRAY);
            if (running) postInvalidateOnAnimation();
        }
        private void bitmap(Canvas c, Bitmap img, float x, float y, float r) {
            if (img == null) return;
            paint.setColor(Color.WHITE); paint.setAlpha(220); c.drawBitmap(img, null, new RectF(x-r,y-r,x+r,y+r), paint); paint.setAlpha(255);
        }
        private void touch(int action, int id, float x, float y) {
            if (handle == 0) return;
            float[] position = nativeTouch(handle, action, id, x, y, getWidth(), getHeight(), density);
            if (position != null && position.length == 2) { stickX = position[0]; stickY = position[1]; renderMoveX = stickX; renderMoveY = -stickY; }
        }
        private void cancelUiTouch() {
            if (uiPointer != -1) uiTouches.add(new UiTouch(3, 0, 0));
            uiPointer = -1;
        }
        @Override public boolean onTouchEvent(MotionEvent e) {
            int action = e.getActionMasked(), index = e.getActionIndex();
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                int id = e.getPointerId(index); float x = e.getX(index), y = e.getY(index);
                // The Vulkan settings panel owns the top-right region; the stick and clip button own the bottom.
                if (vulkanActive && uiPointer == -1 && x >= getWidth() * .65f && y < getHeight() * .50f) {
                    uiPointer = id; uiTouches.add(new UiTouch(0, x, y));
                } else touch(0, id, x, y);
            } else if (action == MotionEvent.ACTION_MOVE) {
                // Every pointer is updated independently; Android may reorder pointer indices between events.
                for (int i = 0; i < e.getPointerCount(); ++i) {
                    int id = e.getPointerId(i); float x = e.getX(i), y = e.getY(i);
                    if (id == uiPointer) uiTouches.add(new UiTouch(1, x, y));
                    else touch(1, id, x, y);
                }
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                int id = e.getPointerId(index); float x = e.getX(index), y = e.getY(index);
                if (id == uiPointer) { uiTouches.add(new UiTouch(2, x, y)); uiPointer = -1; }
                else touch(2, id, x, y);
            } else if (action == MotionEvent.ACTION_CANCEL) { touch(3, -1, 0, 0); cancelUiTouch(); }
            invalidate(); return true;
        }
    }
}
