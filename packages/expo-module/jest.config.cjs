/**
 * Jest config for the TypeScript API surface of sezo-audio-engine.
 *
 * The native module is never loaded: tests replace `expo-modules-core` with a
 * fake (see test/fakeNativeModule.ts), so this runs on plain Node in CI.
 * `.cjs` because the package is `"type": "module"`.
 */
/** @type {import('jest').Config} */
module.exports = {
  testEnvironment: 'node',
  roots: ['<rootDir>/src', '<rootDir>/test'],
  testMatch: ['<rootDir>/test/**/*.test.ts'],
  transform: {
    '^.+\\.ts$': ['ts-jest', { tsconfig: '<rootDir>/test/tsconfig.json' }],
  },
  clearMocks: true,
  collectCoverageFrom: ['src/**/*.ts'],
  coverageThreshold: {
    global: { statements: 100, branches: 100, functions: 100, lines: 100 },
  },
};
