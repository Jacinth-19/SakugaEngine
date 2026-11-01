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

/** Animation-only Android preview: real native matcher, synthetic rig, Canvas visualization. */
public final class MainActivity extends Activity {
    static { System.loadLibrary("sakuga_mobile"); }
    private static native long nativeCreate(String[] paths);
    private static native void nativeDestroy(long handle);
    private static native void nativeUpdate(long handle, float dt, float x, float y);
    private static native void nativeNext(long handle);
    private static native float[] nativeJoints(long handle);
    private static native String nativeInfo(long handle);
    private static native String nativeProbeSurface(Surface surface);
    private volatile boolean destroyed;
    private int surfaceGeneration;
    private PreviewView preview;

    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        preview = new PreviewView();
        // The SurfaceView is underneath the animation preview. It gives the native Vulkan port an actual Android
        // surface to probe without pretending the production renderer is already running.
        FrameLayout layout = new FrameLayout(this);
        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) {
                final int generation = ++surfaceGeneration;
                final Surface surface = holder.getSurface();
                preview.setSurfaceStatus("Checking Android Vulkan surface...");
                new Thread(new Runnable() { @Override public void run() {
                    final String status = surface.isValid() ? nativeProbeSurface(surface) : "Android surface unavailable";
                    runOnUiThread(new Runnable() { @Override public void run() {
                        if (!destroyed && generation == surfaceGeneration) preview.setSurfaceStatus(status);
                    }});
                }}, "vulkan-surface-probe").start();
            }
            @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) { }
            @Override public void surfaceDestroyed(SurfaceHolder holder) {
                ++surfaceGeneration;
                if (!destroyed) preview.setSurfaceStatus("Android surface inactive");
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
                handle = nativeCreate(paths);
                if (handle == 0) error = "Could not load motion data. See Android logcat.";
            } catch (Exception e) { error = "Could not start preview: " + e.getMessage(); }
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
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final int[][] bones = {{0,1},{1,2},{2,3},{3,4},{4,5},{3,6},{6,7},{7,8},{8,9},
                {3,10},{10,11},{11,12},{12,13},{0,14},{14,15},{15,16},{16,17},{0,18},{18,19},{19,20},{20,21}};
        private final Bitmap base, knob;
        private long handle; private long lastNs; private boolean running;
        private String message = "Loading two sample animations...";
        private String surfaceStatus = "Waiting for Android surface...";
        private int pointer = -1; private float stickX, stickY;
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
        void setRunning(boolean enabled) { running = enabled; lastNs = 0; if (enabled) invalidate(); }
        void close() { running = false; if (handle != 0) { nativeDestroy(handle); handle = 0; } }
        private void text(Canvas c, String value, float x, float y, float size, int color) {
            paint.setStyle(Paint.Style.FILL); paint.setColor(color); paint.setTextSize(size * density); c.drawText(value, x, y, paint);
        }
        @Override protected void onDraw(Canvas c) {
            super.onDraw(c); int w = getWidth(), h = getHeight();
            float margin = 18 * density;
            text(c, "SAKUGA  /  ANIMATION PREVIEW", margin, 28 * density, 17, Color.rgb(184,235,250));
            text(c, "2 sample clips  •  synthetic rig  •  not the Vulkan renderer", margin, 48 * density, 11, Color.LTGRAY);
            if (handle == 0) { text(c, message == null ? "Loading..." : message, margin, h * .5f, 14, Color.WHITE); return; }
            long now = System.nanoTime(); float dt = lastNs == 0 ? 0 : (now - lastNs) / 1000000000f; lastNs = now;
            if (running) nativeUpdate(handle, dt, stickX, -stickY);
            float[] j = nativeJoints(handle);
            if (j != null && j.length == 66) {
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
        private void moveStick(float x, float y) {
            float r = Math.min(getWidth(),getHeight())*.13f, bx=18*density+r, by=getHeight()-18*density-r;
            stickX=(x-bx)/r; stickY=(y-by)/r; float length=(float)Math.hypot(stickX,stickY);
            if (length>1) {stickX/=length; stickY/=length;} invalidate();
        }
        @Override public boolean onTouchEvent(MotionEvent e) {
            int action=e.getActionMasked(), i=e.getActionIndex();
            if (action==MotionEvent.ACTION_DOWN || action==MotionEvent.ACTION_POINTER_DOWN) {
                float x=e.getX(i), y=e.getY(i);
                if (handle != 0 && x>getWidth()*.70f && y>getHeight()*.55f) nativeNext(handle);
                else if (pointer==-1 && x<getWidth()*.45f && y>getHeight()*.45f) {pointer=e.getPointerId(i); moveStick(x,y);}
                return true;
            }
            if (action==MotionEvent.ACTION_MOVE && pointer!=-1) { int index=e.findPointerIndex(pointer); if(index>=0) moveStick(e.getX(index),e.getY(index)); return true; }
            if (action==MotionEvent.ACTION_CANCEL || ((action==MotionEvent.ACTION_UP || action==MotionEvent.ACTION_POINTER_UP) && e.getPointerId(i)==pointer)) {
                pointer=-1; stickX=stickY=0; invalidate(); return true;
            }
            return true;
        }
    }
}
