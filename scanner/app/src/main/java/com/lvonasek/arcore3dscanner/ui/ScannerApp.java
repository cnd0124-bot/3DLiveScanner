package com.lvonasek.arcore3dscanner.ui;

import android.app.Application;

public class ScannerApp extends Application {
  private static ScannerApp sInstance;

  @Override
  public void onCreate() {
    super.onCreate();
    sInstance = this;
  }

  public static ScannerApp getInstance() {
    return sInstance;
  }
}
