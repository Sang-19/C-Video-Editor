import express from 'express';
import { execFile, spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { promisify } from 'node:util';
import { createServer as createViteServer } from 'vite';

const execFileAsync = promisify(execFile);
const rootDir = process.cwd();
const cliBinPath = path.join(rootDir, '.build', 'trisvax_cli');

async function ensureCppCliBuilt(): Promise<string | null> {
  if (fs.existsSync(cliBinPath)) {
    return cliBinPath;
  }
  try {
    fs.mkdirSync(path.join(rootDir, '.build'), { recursive: true });
    const sources = [
      'native/engine/types.cpp',
      'native/timeline/timeline_engine.cpp',
      'native/filters/filter_pipeline.cpp',
      'native/rendering/compositor_math.cpp',
      'native/audio/audio_mixer.cpp',
      'native/storage/json_codec.cpp',
      'native/storage/project_store.cpp',
      'native/export/export_engine.cpp',
      'native/engine/video_editor_engine.cpp',
      'native/cli/trisvax_cli.cpp',
    ];
    await execFileAsync(
      'g++',
      ['-std=c++20', '-O2', '-I.', ...sources, '-o', cliBinPath],
      { cwd: rootDir }
    );
    return cliBinPath;
  } catch {
    return null;
  }
}

function runCppCli(subcommand: string, stdinJson?: string): Promise<string> {
  return new Promise((resolve, reject) => {
    const child = spawn(cliBinPath, [subcommand], { cwd: rootDir });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (chunk) => {
      stdout += String(chunk);
    });
    child.stderr.on('data', (chunk) => {
      stderr += String(chunk);
    });
    child.on('error', (err) => reject(err));
    child.on('close', (code) => {
      if (code === 0) {
        resolve(stdout);
      } else {
        reject(new Error(stderr || `trisvax_cli exited with code ${code}`));
      }
    });
    if (stdinJson !== undefined) {
      child.stdin.write(stdinJson);
    }
    child.stdin.end();
  });
}

async function probeAudioDurationMs(filePath: string): Promise<number> {
  try {
    const { stdout } = await execFileAsync('ffprobe', [
      '-v',
      'error',
      '-show_entries',
      'format=duration',
      '-of',
      'default=noprint_wrappers=1:nokey=1',
      filePath,
    ]);
    const sec = parseFloat(stdout.trim());
    if (Number.isFinite(sec) && sec > 0) {
      return Math.round(sec * 1000);
    }
  } catch {
    // fallback if ffprobe unavailable
  }
  return 15000;
}

async function startServer() {
  const app = express();
  const PORT = 3000;

  await ensureCppCliBuilt();

  // Bundled studio music tracks in /public/assets/music
  app.get('/api/music-catalog', async (_req, res) => {
    const musicDir = path.join(rootDir, 'public', 'assets', 'music');
    if (!fs.existsSync(musicDir)) {
      res.json({ tracks: [] });
      return;
    }
    const files = fs
      .readdirSync(musicDir)
      .filter((f) => /\.(mp3|m4a|wav|ogg)$/i.test(f));

    const tracks = await Promise.all(
      files.map(async (fileName) => {
        const title = fileName
          .replace(/\.[^.]+$/, '')
          .replace(/_/g, ' ')
          .replace(/\b\w/g, (ch) => ch.toUpperCase());
        const durationMs = await probeAudioDurationMs(path.join(musicDir, fileName));
        return {
          id: `bundled_${fileName}`,
          title,
          url: `/assets/music/${fileName}`,
          durationMs,
        };
      })
    );
    res.json({ tracks });
  });

  // C++20 Core Engine capability query
  app.get('/api/engine/info', async (_req, res) => {
    try {
      const bin = await ensureCppCliBuilt();
      if (!bin) {
        res.status(503).json({ error: 'C++20 trisvax_cli binary unavailable' });
        return;
      }
      const raw = await runCppCli('info');
      res.json(JSON.parse(raw));
    } catch (err) {
      res.status(500).json({ error: 'Engine query failed', detail: String(err) });
    }
  });

  // Validate & recompute timeline microsecond metrics via C++20 trisvax_cli
  app.post('/api/engine/validate-timeline', express.json({ limit: '5mb' }), async (req, res) => {
    try {
      const bin = await ensureCppCliBuilt();
      if (!bin) {
        res.status(503).json({ error: 'C++20 trisvax_cli binary unavailable' });
        return;
      }
      const raw = await runCppCli('validate-timeline', JSON.stringify(req.body ?? {}));
      res.json(JSON.parse(raw));
    } catch (err) {
      res.status(400).json({ error: 'Timeline validation failed', detail: String(err) });
    }
  });

  // Normalize export settings via C++20 trisvax_cli (1080p, 30 FPS, no watermark)
  app.post('/api/engine/normalize-export', express.json({ limit: '5mb' }), async (req, res) => {
    try {
      const bin = await ensureCppCliBuilt();
      if (!bin) {
        res.status(503).json({ error: 'C++20 trisvax_cli binary unavailable' });
        return;
      }
      const raw = await runCppCli('normalize-export', JSON.stringify(req.body ?? {}));
      res.json(JSON.parse(raw));
    } catch (err) {
      res.status(400).json({ error: 'Export normalization failed', detail: String(err) });
    }
  });

  // Package full C++20 + Android NDK/CMake/JNI/Compose project into a downloadable ZIP
  app.get('/api/android-project.zip', async (_req, res) => {
    const tmpZip = path.join(os.tmpdir(), `trisvax-studio-android-cpp-${Date.now()}.zip`);
    try {
      await execFileAsync('python3', [
        '-c',
        `import os, sys, zipfile
root = sys.argv[1]
dst = sys.argv[2]
include_dirs = ['android', 'native', 'tests', 'docs']
include_files = ['CMakeLists.txt', 'README.md']
exclude_dirs = {'.gradle', 'build', '.idea', '.build', 'node_modules', 'dist', '.git'}
with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as zf:
    for f in include_files:
        p = os.path.join(root, f)
        if os.path.isfile(p):
            zf.write(p, f)
    for d in include_dirs:
        base = os.path.join(root, d)
        if not os.path.isdir(base):
            continue
        for cur, dirs, files in os.walk(base):
            dirs[:] = [x for x in dirs if x not in exclude_dirs]
            for fn in files:
                full = os.path.join(cur, fn)
                rel = os.path.relpath(full, root)
                zf.write(full, rel)
`,
        rootDir,
        tmpZip,
      ]);
      res.setHeader('Content-Type', 'application/zip');
      res.setHeader('Content-Disposition', 'attachment; filename="trisvax-studio-android-cpp.zip"');
      const stream = fs.createReadStream(tmpZip);
      stream.pipe(res);
      stream.on('close', () => {
        fs.unlink(tmpZip, () => {});
      });
    } catch (err) {
      fs.unlink(tmpZip, () => {});
      res.status(500).json({ error: 'Failed to archive Android C++ project', detail: String(err) });
    }
  });

  // Transcode rendered timeline WebM stream to genuine 1080p H.264/AAC MP4 (no watermark)
  app.post(
    '/api/export-mp4',
    express.raw({ type: '*/*', limit: '250mb' }),
    async (req, res) => {
      const rawBuffer = req.body as Buffer;
      if (!Buffer.isBuffer(rawBuffer) || rawBuffer.length === 0) {
        res.status(400).json({ error: 'Empty video payload' });
        return;
      }
      const width = Math.min(1920, Math.max(480, Number(req.query.width) || 1080));
      const height = Math.min(1920, Math.max(480, Number(req.query.height) || 1920));
      const fps = Math.min(60, Math.max(24, Number(req.query.fps) || 30));
      const stamp = Date.now();
      const inputWebm = path.join(os.tmpdir(), `trisvax-in-${stamp}.webm`);
      const outputMp4 = path.join(os.tmpdir(), `trisvax-out-${stamp}.mp4`);

      try {
        fs.writeFileSync(inputWebm, rawBuffer);
        await execFileAsync('ffmpeg', [
          '-y',
          '-i',
          inputWebm,
          '-vf',
          `scale=${width}:${height}:force_original_aspect_ratio=decrease,pad=${width}:${height}:(ow-iw)/2:(oh-ih)/2:color=#0B0807`,
          '-r',
          String(fps),
          '-c:v',
          'libx264',
          '-preset',
          'veryfast',
          '-crf',
          '21',
          '-pix_fmt',
          'yuv420p',
          '-c:a',
          'aac',
          '-b:a',
          '160k',
          '-movflags',
          '+faststart',
          outputMp4,
        ]);
        const mp4Bytes = fs.readFileSync(outputMp4);
        res.setHeader('Content-Type', 'video/mp4');
        res.setHeader('Content-Disposition', 'attachment; filename="TRISVAX_Export_1080p.mp4"');
        res.send(mp4Bytes);
      } catch (err) {
        res.status(500).json({ error: 'FFmpeg 1080p MP4 transcode failed', detail: String(err) });
      } finally {
        fs.unlink(inputWebm, () => {});
        fs.unlink(outputMp4, () => {});
      }
    }
  );

  // Explicit 404 for unknown /api routes
  app.all('/api/*', (_req, res) => {
    res.status(404).json({ error: 'API route not found' });
  });

  if (process.env.NODE_ENV !== 'production') {
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: 'spa',
    });
    app.use(vite.middlewares);
  } else {
    const distDir = path.join(rootDir, 'dist');
    app.use(express.static(distDir));
    app.get('*', (_req, res) => {
      res.sendFile(path.join(distDir, 'index.html'));
    });
  }

  app.listen(PORT, '0.0.0.0', () => {
    console.log(`TRISVAX Studio running on http://localhost:${PORT}`);
  });
}

startServer();
