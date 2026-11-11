// @ts-check
const path = require('node:path');
const { defineConfig } = require('@playwright/test');

// Folder served to the browsers: build/web-release/site by default.
const site = process.env.MONSTRES_SITE ||
  path.resolve(__dirname, '../../build/web-release/site');
const port = Number(process.env.MONSTRES_PORT || 8090);

module.exports = defineConfig({
  testDir: './specs',
  timeout: 60_000,
  fullyParallel: true,
  // The browsers render on the CPU (no GPU in headless mode): a few at a time.
  workers: 3,
  reporter: [['list'], ['html', { open: 'never' }]],
  expect: {
    // Software GPUs differ a little between machines: allow tiny differences.
    toHaveScreenshot: { maxDiffPixelRatio: 0.01, timeout: 20_000 },
    timeout: 15_000,
  },
  use: {
    baseURL: `http://127.0.0.1:${port}/`,
    viewport: { width: 800, height: 600 },
    trace: 'retain-on-failure',
  },
  webServer: {
    command: `python3 -m http.server ${port} --bind 127.0.0.1 --directory "${site}"`,
    url: `http://127.0.0.1:${port}/`,
    reuseExistingServer: !process.env.CI,
  },
  projects: [
    {
      name: 'chromium',
      use: {
        browserName: 'chromium',
        // WebGL2 on SwiftShader (software GPU) in headless mode.
        launchOptions: { args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] },
      },
    },
    { name: 'firefox', use: { browserName: 'firefox' } },
    { name: 'webkit', use: { browserName: 'webkit' } },
  ],
});
