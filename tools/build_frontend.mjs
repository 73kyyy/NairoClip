#!/usr/bin/env node
/* NairoClip 前端构建（P-11）
   合并 + 压缩 + 指纹，不改写现有代码结构（assets/*.js 仍是 IIFE）。
   收益：每页 5~8 个请求降到 1 个、文件名带 hash 可长缓存、构建期能发现
   「页面引用了不存在的脚本」。用法：npm run build / npm run build:dev */
import { readFile, writeFile, mkdir, readdir, copyFile, rm } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import path from 'node:path';
import process from 'node:process';

const ROOT = process.cwd();
const SRC_PAGES = path.join(ROOT, 'mobile/pages');
const SRC_ASSETS = path.join(ROOT, 'mobile/assets');
const OUT = path.join(ROOT, 'dist');
const dev = process.argv.includes('--dev');

let esbuild = null;
if (!dev) {
  try { esbuild = await import('esbuild'); }
  catch { console.error('缺少 esbuild，请先 npm install'); process.exit(1); }
}

const hash = (s) => createHash('sha256').update(s).digest('hex').slice(0, 8);
const SCRIPT_RE = /<script\s+src="\.\.\/assets\/([^"]+)"\s*><\/script>/g;
const LINK_RE = /<link\s+rel="stylesheet"\s+href="\.\.\/assets\/([^"]+)"\s*>/g;

async function build() {
  await rm(OUT, { recursive: true, force: true });
  await mkdir(path.join(OUT, 'assets'), { recursive: true });
  await mkdir(path.join(OUT, 'pages'), { recursive: true });

  const assets = await readdir(SRC_ASSETS);
  const pages = (await readdir(SRC_PAGES)).filter((f) => f.endsWith('.html'));
  const jsFiles = assets.filter((f) => f.endsWith('.js'));
  const cssFiles = assets.filter((f) => f.endsWith('.css'));

  for (const page of pages) {
    const html = await readFile(path.join(SRC_PAGES, page), 'utf8');
    const scripts = [...html.matchAll(SCRIPT_RE)].map((m) => m[1]);
    for (const s of scripts) {
      if (!jsFiles.includes(s)) throw new Error(page + ' 引用了不存在的脚本：' + s);
    }
    let bundle = '';
    for (const s of scripts) {
      bundle += '\n/* ==== ' + s + ' ==== */\n' + (await readFile(path.join(SRC_ASSETS, s), 'utf8'));
    }
    let code = bundle;
    if (!dev && esbuild) code = (await esbuild.transform(bundle, { minify: true, target: 'es2020' })).code;
    const jsName = 'bundle-' + hash(code) + '.js';
    await writeFile(path.join(OUT, 'assets', jsName), code);

    const cssOrder = [...html.matchAll(LINK_RE)].map((m) => m[1]);
    let css = '';
    for (const c of cssOrder) {
      if (!cssFiles.includes(c)) throw new Error(page + ' 引用了不存在的样式：' + c);
      css += '\n/* ==== ' + c + ' ==== */\n' + (await readFile(path.join(SRC_ASSETS, c), 'utf8'));
    }
    const cssName = 'bundle-' + hash(css) + '.css';
    await writeFile(path.join(OUT, 'assets', cssName), css);

    let out = html.replace(SCRIPT_RE, '').replace(LINK_RE, '');
    out = out.replace('</head>', '  <link rel="stylesheet" href="../assets/' + cssName + '">\n</head>');
    out = out.replace('</body>', '  <script src="../assets/' + jsName + '"></script>\n</body>');
    await writeFile(path.join(OUT, 'pages', page), out);
  }

  for (const f of assets) {
    if (f.endsWith('.js') || f.endsWith('.css')) continue;
    await copyFile(path.join(SRC_ASSETS, f), path.join(OUT, 'assets', f));
  }
  console.log('✓ 构建完成：' + pages.length + ' 个页面 → dist/（' + (dev ? 'dev' : 'minified') + '）');
}

build().catch((e) => { console.error('构建失败：', e.message); process.exit(1); });
