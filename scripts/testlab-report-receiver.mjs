#!/usr/bin/env node

import http from 'node:http';
import fs from 'node:fs/promises';
import path from 'node:path';

const port = Number(process.env.SEZO_TESTLAB_REPORT_PORT ?? 8099);
const reportPath = process.env.SEZO_TESTLAB_REPORT_PATH ?? '/testlab/report';
const outputDir = process.env.SEZO_TESTLAB_REPORT_DIR
  ? path.resolve(process.env.SEZO_TESTLAB_REPORT_DIR)
  : path.resolve('artifacts/testlab-reports');
const maxBodyBytes = Number(process.env.SEZO_TESTLAB_REPORT_MAX_BYTES ?? 2_000_000);

const withCors = (res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'POST, OPTIONS, GET');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
};

const writeJson = (res, statusCode, payload) => {
  withCors(res);
  res.statusCode = statusCode;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.end(JSON.stringify(payload));
};

const sanitizeFileName = (name) => {
  const base = String(name || '').replace(/[^a-zA-Z0-9._-]/g, '_').slice(0, 160);
  if (!base) {
    return `testlab-report-${Date.now()}.json`;
  }
  return base.endsWith('.json') ? base : `${base}.json`;
};

const collectRequestBody = async (req) => {
  const chunks = [];
  let total = 0;
  for await (const chunk of req) {
    total += chunk.length;
    if (total > maxBodyBytes) {
      throw new Error(`Payload exceeds ${maxBodyBytes} bytes`);
    }
    chunks.push(chunk);
  }
  return Buffer.concat(chunks).toString('utf8');
};

const server = http.createServer(async (req, res) => {
  if (!req.url) {
    writeJson(res, 400, { ok: false, error: 'missing url' });
    return;
  }

  if (req.method === 'OPTIONS') {
    withCors(res);
    res.statusCode = 204;
    res.end();
    return;
  }

  if (req.method === 'GET' && req.url === '/health') {
    writeJson(res, 200, { ok: true, outputDir, reportPath });
    return;
  }

  if (req.method !== 'POST' || req.url !== reportPath) {
    writeJson(res, 404, { ok: false, error: 'route not found' });
    return;
  }

  try {
    const raw = await collectRequestBody(req);
    const payload = JSON.parse(raw || '{}');
    const report = payload.report ?? payload;
    const fileName = sanitizeFileName(payload.fileName);
    const targetPath = path.join(outputDir, fileName);

    await fs.mkdir(outputDir, { recursive: true });
    await fs.writeFile(
      targetPath,
      JSON.stringify(
        {
          receivedAt: new Date().toISOString(),
          source: 'testlab-dev-mirror',
          savedAt: typeof payload.savedAt === 'string' ? payload.savedAt : new Date().toISOString(),
          report,
        },
        null,
        2
      ),
      'utf8'
    );

    console.log(`[testlab-receiver] saved ${targetPath}`);
    writeJson(res, 200, { ok: true, savedPath: targetPath });
  } catch (error) {
    writeJson(res, 400, {
      ok: false,
      error: error instanceof Error ? error.message : 'invalid request',
    });
  }
});

server.listen(port, '0.0.0.0', () => {
  console.log(`[testlab-receiver] listening on http://0.0.0.0:${port}${reportPath}`);
  console.log(`[testlab-receiver] writing reports to ${outputDir}`);
});
