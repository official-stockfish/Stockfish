#!/usr/bin/env node
/**
 * generate-screenshots.mjs
 * Capture full-page screenshots of a running web app with Playwright.
 *
 * Usage:
 *   node scripts/generate-screenshots.mjs --url http://127.0.0.1:4173 --out screenshots/neuralchess --name neuralchess --widths 1280,1920,390
 */

import { chromium } from 'playwright';
import { mkdir, writeFile } from 'node:fs/promises';
import { join } from 'node:path';
import { parseArgs } from 'node:util';

const { values } = parseArgs({
  options: {
    url: { type: 'string', default: 'http://127.0.0.1:4173' },
    out: { type: 'string', default: 'screenshots' },
    name: { type: 'string', default: 'app' },
    widths: { type: 'string', default: '1280,1920,390' },
    wait: { type: 'string', default: '2000' },
  },
});

const url = values.url;
const outDir = values.out;
const name = values.name;
const widths = values.widths.split(',').map((w) => parseInt(w.trim(), 10)).filter(Boolean);
const waitMs = parseInt(values.wait, 10) || 2000;

async function main() {
  await mkdir(outDir, { recursive: true });

  const browser = await chromium.launch({
    headless: true,
    args: ['--no-sandbox', '--disable-setuid-sandbox'],
  });

  const results = [];

  for (const width of widths) {
    const height = width <= 500 ? 844 : 800;
    const context = await browser.newContext({
      viewport: { width, height },
      deviceScaleFactor: 1,
    });
    const page = await context.newPage();

    try {
      console.log(`Navigating to ${url} @ ${width}x${height}...`);
      await page.goto(url, { waitUntil: 'networkidle', timeout: 60000 });
      await page.waitForTimeout(waitMs);

      const file = join(outDir, `${name}-${width}px.png`);
      await page.screenshot({
        path: file,
        fullPage: true,
        type: 'png',
      });
      console.log(`Saved ${file}`);
      results.push({ width, height, file });
    } catch (err) {
      console.error(`Failed at width ${width}:`, err.message);
      // Still try a partial capture
      try {
        const file = join(outDir, `${name}-${width}px-error.png`);
        await page.screenshot({ path: file, fullPage: false, type: 'png' });
        results.push({ width, height, file, error: err.message });
      } catch (_) {}
    } finally {
      await context.close();
    }
  }

  await browser.close();

  const meta = {
    url,
    name,
    generatedAt: new Date().toISOString(),
    screenshots: results,
  };
  await writeFile(join(outDir, `${name}-meta.json`), JSON.stringify(meta, null, 2));
  console.log('Done.', results.length, 'screenshot(s)');
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
