// Expo config plugin for sezo-audio-engine.
//
// Android: when this package sits in a checkout of the SezoAudioEngine repo
// (a git clone or submodule), the C++ core lives next to it in
// packages/android-engine/android/engine. In that case the plugin adds it to
// the app's Gradle build as `:android-engine`, and the module's build.gradle
// links that project instead of the JitPack artifact, so native changes to the
// core are picked up without a JitPack release. Installed from npm, the
// directory is absent and nothing changes: the JitPack dependency is used.
//
// Opt out with the plugin option `{ android: { engineFromSource: false } }` or
// the environment variable SEZO_AUDIO_ENGINE_FROM_SOURCE=0.
//
// Android also declares a `microphone` foreground service (and the
// FOREGROUND_SERVICE_MICROPHONE permission) that keeps a recording alive in the
// background. Apps that never record can remove both from the merged manifest
// with `{ android: { recordingForegroundService: false } }`.

import fs from 'node:fs';
import { createRequire } from 'node:module';
import path from 'node:path';

const PACKAGE_NAME = 'sezo-audio-engine';
const ENGINE_PROJECT = ':android-engine';
const BLOCK_BEGIN = '// @generated begin sezo-audio-engine engine-from-source';
const BLOCK_END = '// @generated end sezo-audio-engine engine-from-source';
const RECORDING_SERVICE = 'expo.modules.audioengine.RecordingForegroundService';
const MICROPHONE_FGS_PERMISSION = 'android.permission.FOREGROUND_SERVICE_MICROPHONE';
const TOOLS_NAMESPACE = 'http://schemas.android.com/tools';

function projectRootOf(config) {
  return (config._internal && config._internal.projectRoot) || process.cwd();
}

function sourceBuildEnabled(props) {
  const env = process.env.SEZO_AUDIO_ENGINE_FROM_SOURCE;
  if (env !== undefined && ['0', 'false', 'no', 'off'].includes(env.trim().toLowerCase())) {
    return false;
  }
  return !(props && props.android && props.android.engineFromSource === false);
}

function recordingServiceEnabled(props) {
  return !(props && props.android && props.android.recordingForegroundService === false);
}

function addRemoveEntry(list, name) {
  const entries = list || [];
  const existing = entries.find((entry) => entry.$ && entry.$['android:name'] === name);
  if (existing) {
    existing.$['tools:node'] = 'remove';
  } else {
    entries.push({ $: { 'android:name': name, 'tools:node': 'remove' } });
  }
  return entries;
}

/**
 * Marks the recording foreground service and the FOREGROUND_SERVICE_MICROPHONE
 * permission `tools:node="remove"` in an AndroidManifest (xml2js shape), so the
 * manifest merger drops the module's declarations. Idempotent.
 */
export function removeRecordingServiceFromManifest(androidManifest) {
  const manifest = androidManifest.manifest;
  manifest.$ = manifest.$ || {};
  manifest.$['xmlns:tools'] = TOOLS_NAMESPACE;
  manifest['uses-permission'] = addRemoveEntry(manifest['uses-permission'], MICROPHONE_FGS_PERMISSION);
  if (!manifest.application || manifest.application.length === 0) {
    manifest.application = [{ $: {} }];
  }
  const application = manifest.application[0];
  application.service = addRemoveEntry(application.service, RECORDING_SERVICE);
  return androidManifest;
}

// The package root as the app resolves it (workspaces and submodules resolve
// to the real checkout, not a copy).
function resolvePackageDir(projectRoot) {
  try {
    const requireFromApp = createRequire(path.join(projectRoot, 'package.json'));
    const entry = fs.realpathSync(requireFromApp.resolve(PACKAGE_NAME));
    let dir = path.dirname(entry);
    while (dir !== path.dirname(dir)) {
      const manifest = path.join(dir, 'package.json');
      if (fs.existsSync(manifest) && JSON.parse(fs.readFileSync(manifest, 'utf8')).name === PACKAGE_NAME) {
        return dir;
      }
      dir = path.dirname(dir);
    }
  } catch {
    // Not resolvable from the app: behave as an npm install.
  }
  return null;
}

/** The engine's Gradle module directory, or null when this is not a repo checkout. */
export function findEngineDir(projectRoot) {
  const packageDir = resolvePackageDir(projectRoot);
  if (!packageDir) {
    return null;
  }
  const engineDir = path.resolve(packageDir, '..', 'android-engine', 'android', 'engine');
  if (!fs.existsSync(path.join(engineDir, 'build.gradle'))) {
    return null;
  }
  // The core's third-party code (Oboe, signalsmith) comes from git submodules.
  const oboe = path.join(engineDir, 'src', 'main', 'cpp', 'third_party', 'oboe', 'CMakeLists.txt');
  if (!fs.existsSync(oboe)) {
    console.warn(
      `[${PACKAGE_NAME}] ${engineDir} has no third-party sources; run ` +
        '`git submodule update --init --recursive` to build the Android core from source. ' +
        'Using the JitPack artifact instead.'
    );
    return null;
  }
  return engineDir;
}

/** Adds (or replaces) the `:android-engine` include in settings.gradle(.kts) contents. */
export function addEngineToSettings(contents, engineDirFromAndroidRoot, language = 'groovy') {
  const dir = engineDirFromAndroidRoot.split(path.sep).join('/');
  const body =
    language === 'kt'
      ? `include("${ENGINE_PROJECT}")\nproject("${ENGINE_PROJECT}").projectDir = File(rootDir, "${dir}")`
      : `include('${ENGINE_PROJECT}')\nproject('${ENGINE_PROJECT}').projectDir = new File(rootDir, '${dir}')`;
  const block = `${BLOCK_BEGIN}\n${body}\n${BLOCK_END}`;

  const begin = contents.indexOf(BLOCK_BEGIN);
  const end = contents.indexOf(BLOCK_END);
  if (begin !== -1 && end > begin) {
    return contents.slice(0, begin) + block + contents.slice(end + BLOCK_END.length);
  }
  return `${contents.replace(/\s*$/, '')}\n\n${block}\n`;
}

function withoutRecordingService(config, configPlugins) {
  return configPlugins.withAndroidManifest(config, (modConfig) => {
    modConfig.modResults = removeRecordingServiceFromManifest(modConfig.modResults);
    return modConfig;
  });
}

function withEngineFromSource(config, configPlugins, projectRoot) {
  const engineDir = findEngineDir(projectRoot);
  if (!engineDir) {
    return config;
  }
  return configPlugins.withSettingsGradle(config, (modConfig) => {
    const androidRoot = modConfig.modRequest.platformProjectRoot;
    modConfig.modResults.contents = addEngineToSettings(
      modConfig.modResults.contents,
      path.relative(androidRoot, engineDir),
      modConfig.modResults.language
    );
    return modConfig;
  });
}

export default function withSezoAudioEngine(config, props = {}) {
  const fromSource = sourceBuildEnabled(props);
  const withRecordingService = recordingServiceEnabled(props);
  if (!fromSource && withRecordingService) {
    return config;
  }

  const projectRoot = projectRootOf(config);
  const requireFromApp = createRequire(path.join(projectRoot, 'package.json'));
  const configPlugins = requireFromApp('expo/config-plugins');

  let result = config;
  if (!withRecordingService) {
    result = withoutRecordingService(result, configPlugins);
  }
  if (fromSource) {
    result = withEngineFromSource(result, configPlugins, projectRoot);
  }
  return result;
}
