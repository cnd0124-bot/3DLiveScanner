package com.lvonasek.arcore3dscanner.main;

import android.util.Log;

import com.lvonasek.arcore3dscanner.ui.AbstractActivity;
import com.lvonasek.utils.IO;

import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.Scanner;

public class Exporter
{
  public static final String[] FILE_EXT = {".dataset", ".obj", ".ply", ".dxf"};
  public static final String EXT_DATASET = FILE_EXT[0];
  public static final String EXT_OBJ = FILE_EXT[1];
  public static final String EXT_PLY = FILE_EXT[2];
  public static final String EXT_DXF = FILE_EXT[3];

  public static final int EXPORT_TYPE_FLOORPLAN = -100;
  public static final int EXPORT_TYPE_POINTCLOUD = -200;

  public static String compressModel(File model2share) {
    ArrayList<String> filesToZip = new ArrayList<>();
    for (String s : model2share.list()) {
      File f = new File(model2share, s);
      if (!f.isDirectory()) {
        filesToZip.add(f.getAbsolutePath());
      }
    }

    File zipFile = new File(AbstractActivity.getTempPath(), "upload.scan.zip");
    if (zipFile.exists())
      zipFile.delete();
    String zipOutput = zipFile.getAbsolutePath();
    try {
      IO.zip(filesToZip, zipOutput);
    } catch (Exception e) {
      e.printStackTrace();
    }
    return zipOutput;
  }

  public static File export(File file, String filename) {
    File file2save = null;

    // Check if floorplan DXF or PNG exists in the parent directory
    File parent = file.getParentFile();
    if (parent != null) {
      File dxf = new File(parent, "floorplan.dxf");
      if (dxf.exists()) {
        File targetDxf = new File(AbstractActivity.getPath(false), filename + EXT_DXF);
        IO.copy(dxf, targetDxf);
        Log.d(AbstractActivity.TAG, "Floorplan DXF saved: " + targetDxf.getAbsolutePath());
      }
      File png = new File(parent, "floorplan.png");
      if (png.exists()) {
        File targetPng = new File(AbstractActivity.getPath(false), filename + "_floorplan.png");
        IO.copy(png, targetPng);
        Log.d(AbstractActivity.TAG, "Floorplan PNG saved: " + targetPng.getAbsolutePath());
      }
    }

    if (file.getAbsolutePath().endsWith(Exporter.EXT_OBJ)) {
      file2save = new File(AbstractActivity.getPath(false), filename + Exporter.EXT_OBJ);

      //delete old files during overwrite
      try {
        if (file2save.exists())
          for (String s : Exporter.getObjResources(file2save))
            if (new File(AbstractActivity.getPath(false), s).delete())
              Log.d(AbstractActivity.TAG, "File " + s + " deleted");
      } catch (Exception e) {
        e.printStackTrace();
      }

      //move file from temp into folder
      for (String s : Exporter.getObjResources(file.getAbsoluteFile()))
        if (new File(new File(file.getParent()), s).renameTo(new File(AbstractActivity.getPath(false), s)))
          Log.d(AbstractActivity.TAG, "File " + s + " saved");
      if (file.renameTo(file2save))
        Log.d(AbstractActivity.TAG, "Obj file " + file2save.toString() + " saved.");
    }

    if (file.getAbsolutePath().endsWith(Exporter.EXT_PLY)) {
      file2save = new File(AbstractActivity.getPath(false), filename + Exporter.EXT_PLY);

      //delete old file during overwrite
      if (file2save.exists())
        file2save.delete();

      //move file from temp
      if (file.renameTo(file2save))
        Log.d(AbstractActivity.TAG, "Ply file " + file2save.toString() + " saved.");
    }

    if (file.getAbsolutePath().endsWith(Exporter.EXT_DXF)) {
      file2save = new File(AbstractActivity.getPath(false), filename + Exporter.EXT_DXF);
      if (file2save.exists())
        file2save.delete();
      if (file.renameTo(file2save))
        Log.d(AbstractActivity.TAG, "Dxf file " + file2save.toString() + " saved.");
    }

    //copy GPS file
    File gpsFile = new File(file.getParent(), "position.txt");
    if (gpsFile.exists()) {
      File newGPS = new File(AbstractActivity.getPath(false), "position.txt");
      if (newGPS.exists()) {
        newGPS.delete();
      }
      IO.copy(gpsFile, newGPS);
    }

    return file2save;
  }

  public static boolean convertObjToDxf(File objFile, File targetDxfFile) {
    if (objFile == null || !objFile.exists()) return false;
    File actualObj = objFile.isDirectory() ? AbstractActivity.getModel(objFile) : objFile;
    if (actualObj == null || !actualObj.exists()) return false;

    try {
      ArrayList<float[]> vertices = new ArrayList<>();
      float minY = Float.MAX_VALUE;
      float maxY = -Float.MAX_VALUE;

      if (actualObj.isDirectory()) {
        File[] pclFiles = actualObj.listFiles((dir, name) -> name.endsWith(".pcl"));
        if (pclFiles != null && pclFiles.length > 0) {
          for (File pcl : pclFiles) {
            try (java.io.DataInputStream dis = new java.io.DataInputStream(new java.io.BufferedInputStream(new java.io.FileInputStream(pcl)))) {
              byte[] intBuf = new byte[4];
              if (dis.read(intBuf) == 4) {
                int numPoints = (intBuf[0] & 0xFF) | ((intBuf[1] & 0xFF) << 8) | ((intBuf[2] & 0xFF) << 16) | ((intBuf[3] & 0xFF) << 24);
                byte[] ptBuf = new byte[16];
                for (int p = 0; p < numPoints; p++) {
                  if (dis.read(ptBuf) != 16) break;
                  int ix = (ptBuf[0] & 0xFF) | ((ptBuf[1] & 0xFF) << 8) | ((ptBuf[2] & 0xFF) << 16) | ((ptBuf[3] & 0xFF) << 24);
                  int iy = (ptBuf[4] & 0xFF) | ((ptBuf[5] & 0xFF) << 8) | ((ptBuf[6] & 0xFF) << 16) | ((ptBuf[7] & 0xFF) << 24);
                  int iz = (ptBuf[8] & 0xFF) | ((ptBuf[9] & 0xFF) << 8) | ((ptBuf[10] & 0xFF) << 16) | ((ptBuf[11] & 0xFF) << 24);
                  float x = Float.intBitsToFloat(ix);
                  float y = Float.intBitsToFloat(iy);
                  float z = Float.intBitsToFloat(iz);
                  if (!Float.isNaN(x) && !Float.isNaN(y) && !Float.isNaN(z)) {
                    vertices.add(new float[]{x, y, z});
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                  }
                }
              }
            } catch (Exception ignored) {}
          }
        }
      } else {
        boolean isPly = actualObj.getName().toLowerCase().endsWith(".ply");
        boolean inPlyBody = !isPly;
        java.io.BufferedReader reader = new java.io.BufferedReader(new java.io.FileReader(actualObj));
        String line;
        while ((line = reader.readLine()) != null) {
          if (isPly) {
            if (!inPlyBody) {
              if (line.trim().equals("end_header")) inPlyBody = true;
              continue;
            }
            String[] parts = line.trim().split("\\s+");
            if (parts.length >= 3) {
              try {
                float x = Float.parseFloat(parts[0]);
                float y = Float.parseFloat(parts[1]);
                float z = Float.parseFloat(parts[2]);
                vertices.add(new float[]{x, y, z});
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
              } catch (Exception ignored) {}
            }
          } else if (line.startsWith("v ")) {
            String[] parts = line.trim().split("\\s+");
            if (parts.length >= 4) {
              float x = Float.parseFloat(parts[1]);
              float y = Float.parseFloat(parts[2]);
              float z = Float.parseFloat(parts[3]);
              vertices.add(new float[]{x, y, z});
              if (y < minY) minY = y;
              if (y > maxY) maxY = y;
            }
          }
        }
        reader.close();
      }

      if (vertices.isEmpty()) return false;

      // Cross section slice: wall height (typically between 15% and 85% of total height)
      float sliceMinY = (maxY - minY > 0.3f) ? minY + (maxY - minY) * 0.15f : minY - 100.0f;
      float sliceMaxY = (maxY - minY > 0.3f) ? minY + (maxY - minY) * 0.85f : maxY + 100.0f;

      // 2cm grid resolution (scale = 50 cells per meter)
      float gridScale = 50.0f;
      HashSet<Long> grid = new HashSet<>();
      int minGx = Integer.MAX_VALUE, maxGx = Integer.MIN_VALUE;
      int minGz = Integer.MAX_VALUE, maxGz = Integer.MIN_VALUE;

      for (float[] v : vertices) {
        if (v[1] >= sliceMinY && v[1] <= sliceMaxY) {
          int gx = Math.round(v[0] * gridScale);
          int gz = Math.round(v[2] * gridScale);
          long key = (((long) gx) << 32) | (gz & 0xFFFFFFFFL);
          grid.add(key);
          if (gx < minGx) minGx = gx;
          if (gx > maxGx) maxGx = gx;
          if (gz < minGz) minGz = gz;
          if (gz > maxGz) maxGz = gz;
        }
      }

      if (grid.isEmpty()) {
        for (float[] v : vertices) {
          int gx = Math.round(v[0] * gridScale);
          int gz = Math.round(v[2] * gridScale);
          long key = (((long) gx) << 32) | (gz & 0xFFFFFFFFL);
          grid.add(key);
          if (gx < minGx) minGx = gx;
          if (gx > maxGx) maxGx = gx;
          if (gz < minGz) minGz = gz;
          if (gz > maxGz) maxGz = gz;
        }
      }

      if (grid.isEmpty()) return false;

      // Write DXF
      java.io.BufferedWriter writer = new java.io.BufferedWriter(new java.io.FileWriter(targetDxfFile));
      writer.write("0\nSECTION\n2\nHEADER\n");
      writer.write("9\n$ACADVER\n1\nAC1015\n");
      writer.write("9\n$INSUNITS\n70\n6\n"); // 6 = Meters
      writer.write("0\nENDSEC\n");

      writer.write("0\nSECTION\n2\nTABLES\n");
      writer.write("0\nTABLE\n2\nLAYER\n70\n1\n");
      writer.write("0\nLAYER\n2\nWALLS\n70\n0\n62\n7\n6\nCONTINUOUS\n0\nENDTAB\n");
      writer.write("0\nENDSEC\n");

      writer.write("0\nSECTION\n2\nENTITIES\n");

      // Merge collinear horizontal line segments
      for (int gz = minGz; gz <= maxGz + 1; gz++) {
        int runStart = Integer.MIN_VALUE;
        for (int gx = minGx; gx <= maxGx + 1; gx++) {
          long currKey = (((long) gx) << 32) | (gz & 0xFFFFFFFFL);
          long prevKey = (((long) gx) << 32) | ((gz - 1) & 0xFFFFFFFFL);
          boolean curr = grid.contains(currKey);
          boolean prev = grid.contains(prevKey);
          boolean isBoundary = (curr != prev);

          if (isBoundary) {
            if (runStart == Integer.MIN_VALUE) {
              runStart = gx;
            }
          } else {
            if (runStart != Integer.MIN_VALUE) {
              writer.write("0\nLINE\n8\nWALLS\n");
              writer.write("10\n" + (runStart / gridScale) + "\n20\n" + (gz / gridScale) + "\n30\n0.0\n");
              writer.write("11\n" + (gx / gridScale) + "\n21\n" + (gz / gridScale) + "\n31\n0.0\n");
              runStart = Integer.MIN_VALUE;
            }
          }
        }
        if (runStart != Integer.MIN_VALUE) {
          writer.write("0\nLINE\n8\nWALLS\n");
          writer.write("10\n" + (runStart / gridScale) + "\n20\n" + (gz / gridScale) + "\n30\n0.0\n");
          writer.write("11\n" + ((maxGx + 1) / gridScale) + "\n21\n" + (gz / gridScale) + "\n31\n0.0\n");
        }
      }

      // Merge collinear vertical line segments
      for (int gx = minGx; gx <= maxGx + 1; gx++) {
        int runStart = Integer.MIN_VALUE;
        for (int gz = minGz; gz <= maxGz + 1; gz++) {
          long currKey = (((long) gx) << 32) | (gz & 0xFFFFFFFFL);
          long prevKey = (((long) (gx - 1)) << 32) | (gz & 0xFFFFFFFFL);
          boolean curr = grid.contains(currKey);
          boolean prev = grid.contains(prevKey);
          boolean isBoundary = (curr != prev);

          if (isBoundary) {
            if (runStart == Integer.MIN_VALUE) {
              runStart = gz;
            }
          } else {
            if (runStart != Integer.MIN_VALUE) {
              writer.write("0\nLINE\n8\nWALLS\n");
              writer.write("10\n" + (gx / gridScale) + "\n20\n" + (runStart / gridScale) + "\n30\n0.0\n");
              writer.write("11\n" + (gx / gridScale) + "\n21\n" + (gz / gridScale) + "\n31\n0.0\n");
              runStart = Integer.MIN_VALUE;
            }
          }
        }
        if (runStart != Integer.MIN_VALUE) {
          writer.write("0\nLINE\n8\nWALLS\n");
          writer.write("10\n" + (gx / gridScale) + "\n20\n" + (runStart / gridScale) + "\n30\n0.0\n");
          writer.write("11\n" + (gx / gridScale) + "\n21\n" + ((maxGz + 1) / gridScale) + "\n31\n0.0\n");
        }
      }

      writer.write("0\nENDSEC\n0\nEOF\n");
      writer.close();
      return true;
    } catch (Exception e) {
      Log.e(AbstractActivity.TAG, "convertObjToDxf failed", e);
      return false;
    }
  }

  public static boolean isFolder(String s) {
    for (String ext : Exporter.FILE_EXT) {
      if (s.endsWith(ext)) {
        return false;
      }
    }
    return true;
  }

  public static int getModelType(String filename) {
    for(int i = 0; i < FILE_EXT.length; i++) {
      int begin = filename.length() - FILE_EXT[i].length();
      if (begin >= 0)
        if (filename.substring(begin).contains(FILE_EXT[i]))
          return i;
    }
    return -1;
  }

  public static String getMtlResource(String obj)
  {
    try
    {
      Scanner sc = new Scanner(new FileInputStream(obj));
      while(sc.hasNext()) {
        String line = sc.nextLine();
        if (line.startsWith("mtllib")) {
          return line.substring(7);
        }
      }
      sc.close();
    } catch (Exception e)
    {
      e.printStackTrace();
    }
    return null;
  }

  public static ArrayList<String> getObjResources(File file)
  {
    HashSet<String> files = new HashSet<>();
    ArrayList<String> output = new ArrayList<>();
    String mtlLib = getMtlResource(file.getAbsolutePath());
    if (mtlLib != null) {
      output.add(mtlLib);
      output.add(mtlLib + ".png");
      mtlLib = file.getParent() + "/" + mtlLib;
      try
      {
        Scanner sc = new Scanner(new FileInputStream(mtlLib));
        while(sc.hasNext()) {
          String line = sc.nextLine();
          if (line.startsWith("map_") || line.startsWith("norm")) {
            String filename = line.substring(line.indexOf(" ") + 1);
            if (!files.contains(filename)) {
              files.add(filename);
              output.add(filename);
            }
          }
        }
        sc.close();
      } catch (Exception e)
      {
        e.printStackTrace();
      }
    }
    return output;
  }

  public static void makeStructure(String path) {
    //get list of files
    ArrayList<String> files = AbstractActivity.listFiles(new File(path));
    if (files.isEmpty())
      return;
    Collections.sort(files, String::compareTo);
    ArrayList<String> models = new ArrayList<>();
    for(String s : files)
      if(new File(s).isFile()) {
        int t = getModelType(s);
        if(t >= 0 && t < 3)
          models.add(s);
      }

    //restructure models
    for (String s : models) {

      //create temp folder
      File temp = new File(path, "temp");
      if (temp.exists())
        AbstractActivity.deleteRecursive(temp);
      temp.mkdir();

      //get model files
      ArrayList<String> res = new ArrayList<>();
      File gpsFile = new File(path, "position.txt");
      if (gpsFile.exists())
        res.add(gpsFile.getName());
      if (Exporter.FILE_EXT[getModelType(s)].compareTo(Exporter.EXT_OBJ) == 0) {
        res.addAll(getObjResources(new File(s)));
      }
      res.add(new File(s).getName());

      //restructure
      for (String r : res) {
        File f = new File(path, r);
        if (f.exists())
          f.renameTo(new File(temp, r));
      }
      temp.renameTo(new File(s));
    }
  }
}
