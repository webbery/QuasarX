import { defineConfig, loadEnv } from 'vite'
import vue from '@vitejs/plugin-vue'
import electron from 'vite-plugin-electron'
import renderer from 'vite-plugin-electron-renderer'
import { rmSync, mkdirSync, copyFileSync, existsSync } from 'node:fs'
import { join } from 'node:path'

// https://vitejs.dev/config/
export default defineConfig(({ command, mode }) => {
  rmSync('build/electron', { recursive: true, force: true })

  const isServe = command === 'serve'
  const isBuild = command === 'build'
  const sourcemap = isServe || !!process.env.VSCODE_DEBUG

  process.env = { ...process.env, ...loadEnv(mode, process.cwd()) };

  return {
    server: {
      port: 23678,
      proxy: {
        '/v0': {
          target: 'https://localhost:19107/v0',
          changeOrigin: true,
          rewrite: (path) => {console.info(path); return path.replace(/^\/v0/, '')},
        },
        '/sinajs': {
          target: 'http://hq.sinajs.cn',
          changeOrigin: true,
          rewrite: (path) => {console.info(path); return path.replace(/^\/sinajs/, '')},
        }
      }
    },
    resolve: {
      alias: {
        "@": `${__dirname}/src`,
        "@anthropic-ai/sdk/lib/transform-json-schema": `${__dirname}/node_modules/@anthropic-ai/sdk/lib/transform-json-schema.js`,
      },
    },
    optimizeDeps: {
      include: ['@anthropic-ai/sdk'],
      exclude: ['@lancedb/lancedb'],
    },
    css: {
      preprocessorOptions: {
        scss: {
          additionalData: '@import "src/styles/variables.scss";'
        }
      }
    },
    build: {
      outDir: 'build/app',
      emptyOutDir: true,
    },
    plugins: [
      vue(),
      electron([
        {
          entry: 'electron/main.ts',
          onstart(options) {
            process.env.VSCODE_DEBUG
              ? console.log(/* For `.vscode/.debug.script.mjs` */"[startup] Electron App")
              : options.startup();
          },
          vite: {
            resolve: {
              alias: {
                "@": `${__dirname}/src`,
              },
            },
            build: {
              sourcemap,
              minify: isBuild,
              outDir: 'build/electron',
              rollupOptions: {
                external: ['@lancedb/lancedb', '@xenova/transformers', 'electron', 'electron-store', 'pdf-parse-new', 'node-stream-zip', 'axios', '@langchain/core', '@langchain/openai', '@langchain/anthropic', '@langchain/langgraph', '@langchain/langgraph-checkpoint', 'langchain', '@anthropic-ai/sdk'],
                output: {
                  format: 'esm',
                },
              },
            },
            plugins: [{
              name: 'copy-preload',
              closeBundle() {
                const src = join(__dirname, 'electron/preload.cjs');
                const dst = join(__dirname, 'build/electron/preload.cjs');
                if (existsSync(src)) {
                  mkdirSync('build/electron', { recursive: true });
                  copyFileSync(src, dst);
                  console.log('[copy] preload.cjs → build/electron/');
                }
              },
            }],
          },
        }
      ]),
      // Use Node.js API in the Renderer-process
      renderer(),
    ],
    clearScreen: true,

  }
})
