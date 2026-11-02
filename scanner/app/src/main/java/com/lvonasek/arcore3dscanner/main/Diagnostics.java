package com.lvonasek.arcore3dscanner.main;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.TextView;

import com.google.ar.core.ArCoreApk;
import com.lvonasek.arcore3dscanner.BuildConfig;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * Runtime diagnostics: on-screen status HUD plus log files written to the public Download
 * folder (3DScanner_debug.txt / 3DScanner_logcat.txt) so failures can be analysed off-device.
 */
public class Diagnostics {
  private static final String TAG = "3DScannerDiag";
  private static File sDebugFile;
  private static File sLogcatFile;
  private static Thread sLogcatThread;
  private static java.lang.Process sLogcatProcess;
  private static Handler sHandler;
  private static TextView sHud;
  private static long sLastFileWrite = 0;

  private static File resolveDir(Activity activity) {
    try {
      File dl = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
      File test = new File(dl, "3DScanner_debug.txt");
      FileOutputStream fos = new FileOutputStream(test, true);
      fos.close();
      return dl;
    } catch (Throwable e) {
      Log.w(TAG, "Download not writable, falling back", e);
      return activity.getExternalFilesDir(null);
    }
  }

  public static synchronized void append(String line) {
    Log.i(TAG, line);
    if (sDebugFile == null) return;
    try {
      FileOutputStream fos = new FileOutputStream(sDebugFile, true);
      String ts = new SimpleDateFormat("HH:mm:ss.SSS", Locale.US).format(new Date());
      fos.write((ts + " " + line + "\n").getBytes("UTF-8"));
      fos.close();
    } catch (Throwable ignored) {}
  }

  public static void start(final Activity activity) {
    try {
      File dir = resolveDir(activity);
      sDebugFile = new File(dir, "3DScanner_debug.txt");
      sLogcatFile = new File(dir, "3DScanner_logcat.txt");
      // fresh files per session
      new FileOutputStream(sDebugFile, false).close();
      new FileOutputStream(sLogcatFile, false).close();
    } catch (Throwable e) {
      Log.e(TAG, "Cannot create debug files", e);
    }

    append("=== 3DScanner session start ===");
    append("version=" + BuildConfig.VERSION_NAME + " (" + BuildConfig.VERSION_CODE + ")");
    append("device=" + Build.MANUFACTURER + " " + Build.MODEL + " sdk=" + Build.VERSION.SDK_INT);
    try {
      boolean cam = activity.checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED;
      append("cameraPermission=" + cam);
    } catch (Throwable e) {
      append("cameraPermission=? " + e);
    }
    try {
      ArCoreApk.Availability a = ArCoreApk.getInstance().checkAvailability(activity);
      append("arcoreAvailability=" + a);
    } catch (Throwable e) {
      append("arcoreAvailability=ERR " + e);
    }

    final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
    Thread.setDefaultUncaughtExceptionHandler((t, ex) -> {
      StringWriter sw = new StringWriter();
      ex.printStackTrace(new PrintWriter(sw));
      append("CRASH in thread " + t.getName() + ":\n" + sw);
      if (prev != null) prev.uncaughtException(t, ex);
    });

    startLogcat();
    attachHud(activity);
  }

  private static void startLogcat() {
    if (sLogcatFile == null || sLogcatThread != null) return;
    sLogcatThread = new Thread(() -> {
      try {
        int pid = android.os.Process.myPid();
        sLogcatProcess = Runtime.getRuntime().exec(new String[]{"logcat", "-v", "time", "--pid=" + pid});
        BufferedReader br = new BufferedReader(new InputStreamReader(sLogcatProcess.getInputStream()));
        FileOutputStream fos = new FileOutputStream(sLogcatFile, true);
        String line;
        long written = 0;
        while ((line = br.readLine()) != null) {
          byte[] b = (line + "\n").getBytes("UTF-8");
          fos.write(b);
          written += b.length;
          if (written > 8 * 1024 * 1024) break; // cap at 8MB
        }
        fos.close();
      } catch (Throwable e) {
        append("logcat capture failed: " + e);
      }
    }, "diag-logcat");
    sLogcatThread.setDaemon(true);
    sLogcatThread.start();
  }

  private static void attachHud(final Activity activity) {
    activity.runOnUiThread(() -> {
      try {
        ViewGroup root = activity.findViewById(android.R.id.content);
        sHud = new TextView(activity);
        sHud.setTextColor(Color.WHITE);
        sHud.setBackgroundColor(0x99000000);
        sHud.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        sHud.setPadding(16, 8, 16, 8);
        sHud.setTypeface(android.graphics.Typeface.MONOSPACE);
        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.TOP;
        lp.topMargin = 120;
        root.addView(sHud, lp);
      } catch (Throwable e) {
        append("HUD attach failed: " + e);
      }
      sHandler = new Handler(Looper.getMainLooper());
      sHandler.post(sTick);
    });
  }

  private static final Runnable sTick = new Runnable() {
    @Override
    public void run() {
      String status;
      try {
        status = JNI.getDebugStatus();
      } catch (Throwable e) {
        status = "native status unavailable: " + e;
      }
      String guide = guidance(status);
      if (sHud != null) sHud.setText(guide + "\n" + status);
      long now = System.currentTimeMillis();
      if (now - sLastFileWrite > 2000) {
        sLastFileWrite = now;
        append("STATUS | " + guide + " | " + status.replace('\n', ' '));
      }
      if (sHandler != null) sHandler.postDelayed(this, 500);
    }
  };

  private static String guidance(String s) {
    if (s == null) return "";
    if (s.contains("AR service NULL")) return "[!] AR 서비스가 생성되지 않았습니다";
    if (s.contains("(NULL)")) return "[!] ARCore 세션 생성 실패 (Google Play AR 서비스 확인)";
    if (s.contains("no frame yet")) return "[..] 렌더링 시작 대기 중";
    if (s.contains("tracking=STOPPED")) return "[!] 추적 중지됨 - 앱을 다시 실행하세요";
    if (s.contains("tracking=PAUSED") || s.contains("tracking=NONE")) {
      if (s.contains("fail=2")) return "[!] 너무 어둡습니다 - 조명을 켜세요";
      if (s.contains("fail=3")) return "[!] 너무 빠릅니다 - 천천히 움직이세요";
      if (s.contains("fail=4")) return "[!] 특징점 부족 - 무늬/물체가 있는 곳을 비추세요";
      if (s.contains("fail=5")) return "[!] 카메라 사용 불가 - 다른 카메라 앱을 종료하세요";
      return "[..] 초기화 중 - 휴대폰을 좌우로 천천히 움직이세요";
    }
    if (s.contains("tracking=TRACKING")) {
      if (s.contains("planes=0")) return "[OK] 추적 중 - 바닥/벽을 천천히 비추세요";
      return "[OK] 스캔 중 - 방을 천천히 한 바퀴 돌며 비추세요";
    }
    return "";
  }
}
