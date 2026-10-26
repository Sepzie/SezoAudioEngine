#!/usr/bin/env node
// Print the body of the `## <version>` section of packages/expo-module/CHANGELOG.md.
// Used by the release workflow for GitHub release notes and by CI to check that
// the current package version has a changelog entry. Exits 1 if missing/empty.
//
//   node scripts/changelog-section.mjs 0.0.19
//   node scripts/changelog-section.mjs 0.0.19 path/to/CHANGELOG.md
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const [version, file] = process.argv.slice(2);
if (!version) {
  console.error('usage: changelog-section.mjs <version> [changelog-path]');
  process.exit(2);
}

const changelogPath =
  file ?? fileURLToPath(new URL('../packages/expo-module/CHANGELOG.md', import.meta.url));
const lines = readFileSync(changelogPath, 'utf8').split(/\r?\n/);

const escaped = version.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
// Accepts "## 1.2.3", "## [1.2.3]", "## v1.2.3", "## 1.2.3 - 2026-01-01".
const heading = new RegExp(`^##\\s+\\[?v?${escaped}\\]?(\\s|$)`);

const start = lines.findIndex((line) => heading.test(line));
if (start === -1) {
  console.error(`No "## ${version}" section in ${changelogPath}. Add a changelog entry before releasing.`);
  process.exit(1);
}

let end = lines.findIndex((line, i) => i > start && /^##\s/.test(line));
if (end === -1) end = lines.length;

const body = lines.slice(start + 1, end).join('\n').trim();
if (!body) {
  console.error(`The "## ${version}" section in ${changelogPath} is empty.`);
  process.exit(1);
}

process.stdout.write(`${body}\n`);
