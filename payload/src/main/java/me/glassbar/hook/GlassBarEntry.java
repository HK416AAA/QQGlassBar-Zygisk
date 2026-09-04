package me.glassbar.hook;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.RenderEffect;
import android.graphics.Shader;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;
import java.util.Map;

import mq.core.CoreEngineImpl;

/**
 * QQGlassBar host-side entry (single-payload container).
 * Best-effort: after QQ main window is up it attaches a translucent "glass"
 * pill bar to the bottom of the current activity's decor. All steps are
 * guarded so QQ never crashes because of this module.
 */
public final class GlassBarEntry {
    private static final String TAG = "QQGlassBar";
    private static final String QQ_PKG = "com.tencent.mobileqq";
    private static volatile boolean attached = false;

    private GlassBarEntry() {
    }

    public static void startWithCore(String modulePath, String hostDataDir, ClassLoader coreLoader) {
        try {
            CoreEngineImpl.ensureInit();
            scheduleAttach();
            writeStatus(hostDataDir, "ok\nengine:core\n" + android.os.Process.myPid());
        } catch (Throwable t) {
            Log.e(TAG, "start failed", t);
            writeStatus(hostDataDir, "fail: " + t + "\n" + Log.getStackTraceString(t));
        }
    }

    private static void scheduleAttach() {
        Handler h = new Handler(Looper.getMainLooper());
        Runnable r = new Runnable() {
            int tries = 0;

            @Override
            public void run() {
                if (attached) return;
                if (!qqPackage()) { if (++tries < 60) h.postDelayed(this, 1000); return; }
                try {
                    attachToCurrentActivity();
                } catch (Throwable ignored) {
                }
                if (!attached && ++tries < 60) h.postDelayed(this, 1000);
            }
        };
        h.postDelayed(r, 5000);
    }

    private static boolean qqPackage() {
        try {
            Object at = Class.forName("android.app.ActivityThread")
                    .getMethod("currentActivityThread").invoke(null);
            Object app = at == null ? null
                    : at.getClass().getMethod("getApplication").invoke(at);
            return app != null && QQ_PKG.equals(app.getPackageName());
        } catch (Throwable t) {
            return false;
        }
    }

    private static void attachToCurrentActivity() throws Exception {
        Activity act = currentActivity();
        if (act == null) return;
        ViewGroup decor = (ViewGroup) act.getWindow().getDecorView();
        if (decor == null || decor.getHeight() == 0) return;
        ViewGroup host = decor;
        if (decor.getChildCount() > 0 && decor.getChildAt(0) instanceof ViewGroup) {
            host = (ViewGroup) decor.getChildAt(0);
        }
        FrameLayout bar = buildBar(act);
        float d = act.getResources().getDisplayMetrics().density;
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                (int) (d * 300), (int) (d * 54));
        lp.gravity = Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL;
        lp.bottomMargin = (int) (d * 28);
        bar.setLayoutParams(lp);
        host.addView(bar);
        attached = true;
        Log.i(TAG, "glass bar attached");
    }

    private static Activity currentActivity() {
        try {
            Class<?> kAt = Class.forName("android.app.ActivityThread");
            Object at = kAt.getMethod("currentActivityThread").invoke(null);
            Field f = kAt.getDeclaredField("mActivities");
            f.setAccessible(true);
            Object mapObj = f.get(at);
            if (!(mapObj instanceof Map)) return null;
            Map<?, ?> map = (Map<?, ?>) mapObj;
            Activity best = null;
            for (Object v : map.values()) {
                try {
                    Class<?> rec = v.getClass();
                    Field fa = rec.getDeclaredField("activity");
                    fa.setAccessible(true);
                    Object a = fa.get(v);
                    if (!(a instanceof Activity)) continue;
                    Activity act = (Activity) a;
                    if (act.isFinishing() || act.isDestroyed()) continue;
                    best = act; // last non-finished is most likely current
                } catch (Throwable ignored) {
                }
            }
            return best;
        } catch (Throwable t) {
            return null;
        }
    }

    private static FrameLayout buildBar(Activity act) {
        float d = act.getResources().getDisplayMetrics().density;
        FrameLayout bar = new FrameLayout(act);
        bar.setBackgroundColor(0xD91A1F2A);
        if (Build.VERSION.SDK_INT >= 31) {
            try {
                bar.setRenderEffect(RenderEffect.createBlurEffect(d * 10, d * 10, Shader.TileMode.CLAMP));
            } catch (Throwable ignored) {
            }
        }
        LinearLayout row = new LinearLayout(act);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        FrameLayout.LayoutParams rp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        row.setLayoutParams(rp);
        row.addView(tab(act, "消息"));
        row.addView(tab(act, "联系人"));
        row.addView(tab(act, "动态"));
        bar.addView(row);
        return bar;
    }

    private static TextView tab(Activity act, String text) {
        TextView tv = new TextView(act);
        tv.setText(text);
        tv.setTextColor(Color.WHITE);
        tv.setTextSize(14);
        tv.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1f);
        tv.setLayoutParams(lp);
        return tv;
    }

    private static void writeStatus(String hostDataDir, String status) {
        try {
            File dir = new File(hostDataDir, "files/.a2q");
            if (!dir.exists()) dir.mkdirs();
            FileOutputStream out = new FileOutputStream(new File(dir, "glassbar_status.txt"));
            out.write(status.getBytes(StandardCharsets.UTF_8));
            out.close();
        } catch (Throwable ignored) {
        }
    }
}
