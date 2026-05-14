import { app, BrowserWindow, shell, screen, session, ipcMain, dialog } from 'electron';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { createRequire } from 'module';
import Store from 'electron-store';
import { productName, description, version } from "../package.json";

// ESM 兼容：__dirname 替代
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// ESM 兼容：require 替代（仅用于纯 CJS 模块）
const require = createRequire(import.meta.url);

// import installExtension, { VUEJS_DEVTOOLS } from "electron-devtools-installer";
import axios from 'axios';
import https from 'https';
import { cpSync, mkdirSync, existsSync, writeFileSync, readFileSync, unlinkSync, readdirSync, statSync } from 'fs';
import { initVectorDB, storeChunks, deleteChunks, vectorSearch, clearAll, getStats, shutdownVectorDB, preloadModel, initIntentTable, storeIntents, patchIntents, searchIntents, storeSummary, updateSummaryStatus, getSummaryStatus, deleteSummaryOnly } from './vectorDB';
import { agentRouter } from './agent/AgentRouter';
import { IndexAgent } from './agent/IndexAgent';
import type { AgentConfig } from '../src/lib/agent';

/**
 * ** The built directory structure
 * ------------------------------------
 * ├─┬ build/electron
 * │ └── main.js        > Electron-Main
 * │ └── preload.js     > Preload-Scripts
 * ├─┬ build/app
 *   └── index.html     > Electron-Renderer
 */

process.env.BUILD_APP = join(__dirname, '../app')
process.env.PUBLIC = process.env.VITE_DEV_SERVER_URL
    ? join(__dirname, '../../public')
    : process.env.BUILD_APP

let mainWindow: BrowserWindow | null = null
Store.initRenderer()

function createWindow() {
    const screenSize = screen.getPrimaryDisplay().workAreaSize

    mainWindow = new BrowserWindow({
        icon: join(__dirname, "../../src/assets/icons/icon.png"),
        title: `${productName} | ${description} - v${version}`,
        minWidth: 650,
        minHeight: 550,
        width: 800,
        height: screenSize.height - 150,
        resizable: true,
        maximizable: true,
        webPreferences: {
            // Warning: Enable nodeIntegration and disable contextIsolation is not secure in production
            // Consider using contextBridge.exposeInMainWorld
            // Read more on https://www.electronjs.org/docs/latest/tutorial/context-isolation
            nodeIntegration: true,
            contextIsolation: false,
            webSecurity: false, // 禁用同源策略
            preload: join(__dirname, 'preload.cjs')
        }
    });

    // 隐藏菜单栏
    mainWindow.setMenu(null);

    if (!process.env.VITE_DEV_SERVER_URL) {
      mainWindow.loadFile(join(process.env.BUILD_APP, 'index.html'))
    } else {
      mainWindow.loadURL(process.env.VITE_DEV_SERVER_URL)
      mainWindow.maximize()

      // 页面加载完成后再打开 DevTools，避免过早调用导致失效
      mainWindow.webContents.on('dom-ready', () => {
          mainWindow.webContents.openDevTools();
      }, { once: true });
    }

    // Make all links open with the browser, not with the application
    mainWindow.webContents.setWindowOpenHandler(({ url }) => {
        if (url.startsWith('https:')) shell.openExternal(url)
        return { action: 'deny' }
    })
}

// This method will be called when Electron has finished
// initialization and is ready to create browser windows.
// Some APIs can only be used after this event occurs.
app.whenReady().then(async () => {
    if (process.env.VITE_DEV_SERVER_URL) {
        // Install Vue Devtools  
        // try {
        //     await installExtension(VUEJS_DEVTOOLS);
        // } catch (e) {
        //     console.error("Vue Devtools failed to install:", e.toString());
        // }
    }
    app.commandLine.appendSwitch('ignore-certificate-errors');

    createWindow();

    // header security policy
    session.defaultSession.webRequest.onHeadersReceived((details, callback) => {
        callback({
            responseHeaders: {
                ...details.responseHeaders,
                'Content-Security-Policy': ['script-src \'self\'']
            }
        })
    })

    app.on('activate', () => {
        // On macOS it's common to re-create a window in the app when the
        // dock icon is clicked and there are no other windows open.
        const allWindows = BrowserWindow.getAllWindows()
        allWindows.length === 0 ? createWindow() : allWindows[0].focus();
    });

    // Initialize VectorDB
    try {
        await initVectorDB(getKnowledgeDir());
    } catch (e) {
        console.error('[VectorDB] init failed:', e);
    }

    // Initialize AgentRouter
    try {
        agentRouter.register('index', new IndexAgent());
        console.log('[AgentRouter] IndexAgent registered');
    } catch (e) {
        console.error('[AgentRouter] init failed:', e);
    }

    // 后台预加载嵌入模型（不阻塞窗口显示）
    (async () => {
        const knowledgeDir = getKnowledgeDir();
        try {
            await preloadModel(knowledgeDir, (status) => {
                // 将模型加载进度推送到渲染进程
                mainWindow?.webContents.send('model-load-status', status);
            });
        } catch (e: any) {
            console.warn('[VectorDB] preloadModel background task failed:', e.message);
        }
    })();

    ipcMain.handle('open-directory-dialog', async (event, options) => {
        const result = await dialog.showOpenDialog({
            title: options?.title || '选择目录',
            defaultPath: options?.defaultPath || '',
            buttonLabel: options?.buttonLabel || '选择',
            properties: ['openDirectory']
        });
        if (result.canceled || result.filePaths.length == 0) {
            return null;
        } else {
            return result.filePaths[0];
        }
    });

    ipcMain.handle('merge-csv', async (_, url, token, dstZip, mergeSrc) => {
        console.info('merge', url, dstZip, mergeSrc)
        const agent = new https.Agent({
            rejectUnauthorized: false // 忽略证书错误
        });
        const response = await axios.get(url, {
            httpsAgent: agent,
            responseType: 'arraybuffer',
            headers: { 'Authorization': token}})
        if (response.status === 200) {
            const fs = require('fs');
            const path = require('path');
            const StreamZip = require('node-stream-zip');
            // 解压zip文件
            const zip = new StreamZip.async({ file: dstZip });
            await zip.extract(null, 'zip_temp');
            await zip.close();
            // 递归复制 CBOR 文件到目标文件夹（覆盖已有文件）
            await RecursiveCopyCbor(mergeSrc, 'zip_temp')
            return true;
        } else {
            return false;
        }
    })

    // 处理文件选择请求
    ipcMain.handle('select-file', async (event, options) => {
        try {
            const result = await dialog.showOpenDialog({
            title: options.title || '选择文件',
            defaultPath: options.defaultPath || '',
            buttonLabel: options.buttonLabel || '选择',
            filters: options.filters || [
                { name: '所有文件', extensions: ['*'] }
            ],
            properties: options.properties || ['openFile']
            })
            
            if (!result.canceled && result.filePaths.length > 0) {
            return { success: true, filePath: result.filePaths[0] }
            }
            return { success: false, filePath: null }
        } catch (error) {
            console.error('文件选择错误:', error)
            return { success: false, error: error.message }
        }
    })
});

// Quit when all windows are closed, except on macOS. There, it's common
// for applications and their menu bar to stay active until the user quits
// explicitly with Cmd + Q.
app.on('window-all-closed', async () => {
    mainWindow = null;
    await shutdownVectorDB();
    if (process.platform !== 'darwin') app.quit();
});

function createReadCSVPromise(filepath, ) {
    return 
}
// In this file you can include the rest of your app's specific main process
// code. You can also put them in separate files and require them here.

// ============================================================
// Knowledge Base IPC Handlers
// ============================================================
/**
 * 获取 knowledge 目录路径
 * 使用可执行文件所在目录作为基准
 */
function getKnowledgeDir(): string {
  // 开发环境使用 app 目录，生产环境使用可执行文件所在目录
  const basePath = process.env.VITE_DEV_SERVER_URL
    ? join(__dirname, '../..')
    : dirname(app.getPath('exe'));
  const knowledgeDir = join(basePath, 'knowledge');
  if (!existsSync(knowledgeDir)) {
    mkdirSync(knowledgeDir, { recursive: true });
  }
  return knowledgeDir;
}

/**
 * 生成安全的文件名（冲突时追加时间戳）
 */
function getSafeFileName(dir: string, fileName: string): string {
  let safeName = fileName;
  let counter = 0;
  while (existsSync(join(dir, safeName))) {
    const nameWithoutExt = fileName.replace(/\.[^.]+$/, '');
    const ext = fileName.match(/\.[^.]+$/)?.[0] || '';
    counter += 1;
    safeName = `${nameWithoutExt}_${Date.now()}${counter}${ext}`;
  }
  return safeName;
}

/**
 * 保存 PDF 文件到 knowledge 目录
 * @param {Buffer} fileData - PDF 文件数据
 * @param {string} fileName - 原始文件名
 * @returns {object} { success: boolean, fileName: string, path: string, error?: string }
 */
ipcMain.handle('knowledge-save-pdf', async (_, fileData: Buffer, fileName: string) => {
  try {
    const dir = getKnowledgeDir();
    const safeName = getSafeFileName(dir, fileName);
    const filePath = join(dir, safeName);
    writeFileSync(filePath, Buffer.from(fileData));
    return { success: true, fileName: safeName, path: filePath };
  } catch (error: any) {
    console.error('[knowledge-save-pdf] 错误:', error);
    return { success: false, fileName: '', path: '', error: error.message };
  }
});

/**
 * 列出 knowledge 目录下的所有 PDF 文件
 * @returns {object[]} [{ name: string, size: number, mtime: number, path: string }]
 */
ipcMain.handle('knowledge-list-pdfs', async () => {
  try {
    const dir = getKnowledgeDir();
    const files = readdirSync(dir);
    const pdfs = files
      .filter(f => f.toLowerCase().endsWith('.pdf'))
      .map(f => {
        const filePath = join(dir, f);
        const stat = statSync(filePath);
        return {
          name: f,
          size: stat.size,
          mtime: stat.mtimeMs,
          path: filePath,
        };
      });
    return { success: true, pdfs };
  } catch (error: any) {
    console.error('[knowledge-list-pdfs] 错误:', error);
    return { success: false, pdfs: [], error: error.message };
  }
});

/**
 * 删除 knowledge 目录下的 PDF 文件
 * @param {string} fileName - 文件名
 * @returns {object} { success: boolean, error?: string }
 */
ipcMain.handle('knowledge-delete-pdf', async (_, fileName: string) => {
  try {
    const dir = getKnowledgeDir();
    const filePath = join(dir, fileName);
    if (existsSync(filePath)) {
      unlinkSync(filePath);
      return { success: true };
    }
    return { success: false, error: '文件不存在' };
  } catch (error: any) {
    console.error('[knowledge-delete-pdf] 错误:', error);
    return { success: false, error: error.message };
  }
});

/**
 * 读取 PDF 文件内容（用于下载）
 * @param {string} fileName - 文件名
 * @returns {object} { success: boolean, data?: Buffer, error?: string }
 */
ipcMain.handle('knowledge-read-pdf', async (_, fileName: string) => {
  try {
    const dir = getKnowledgeDir();
    const filePath = join(dir, fileName);
    const data = readFileSync(filePath);
    return { success: true, data: data.toString('base64') };
  } catch (error: any) {
    console.error('[knowledge-read-pdf] 错误:', error);
    return { success: false, error: error.message };
  }
});

/**
 * 获取 knowledge 目录路径
 */
ipcMain.handle('knowledge-get-dir', async () => {
  return { success: true, path: getKnowledgeDir() };
});

/**
 * 解析 PDF 文本内容
 * @param {Buffer} fileData - PDF 文件数据
 * @returns {object} { success: boolean, text: string, pages: number, error?: string }
 */
ipcMain.handle('parse-pdf', async (_, fileData: number[]) => {
  try {
    const pdf = require('pdf-parse-new');
    const buffer = Buffer.from(fileData);
    const data = await pdf(buffer);
    return {
      success: true,
      text: data.text || '',
      pages: data.numpages || 0,
      info: data.info || {},
      metadata: data.metadata || {},
    };
  } catch (error: any) {
    console.error('[parse-pdf] 错误:', error);
    return { success: false, text: '', pages: 0, error: error.message };
  }
});

async function RecursiveCopyCbor(to_dir, from_dir) {
    const path = require('path');
    const fs = require('fs');
    const { mkdirSync, cpSync } = require('fs');
    console.info('copy cbor from', from_dir, 'to', to_dir)
    const subdirs = fs.readdirSync(from_dir)
    for (const item of subdirs) {
        const srcPath = path.join(from_dir, item);
        const dstPath = path.join(to_dir, item);
        const st = fs.statSync(srcPath);
        if (!fs.existsSync(dstPath)) {
            if (st.isDirectory()) {
                mkdirSync(dstPath, { recursive: true });
                RecursiveCopyCbor(dstPath, srcPath);
            }
            else if (st.isFile() && path.extname(srcPath).toLowerCase() === '.cbor') {
                cpSync(srcPath, dstPath);
            }
        } else {
            if (st.isDirectory()) {
                RecursiveCopyCbor(dstPath, srcPath);
            }
            else if (st.isFile() && path.extname(srcPath).toLowerCase() === '.cbor') {
                // CBOR 直接覆盖
                cpSync(srcPath, dstPath);
            }
        }
    }
}

// ============================================================
// Vector Database IPC Handlers (Main Process)
// ============================================================

ipcMain.handle('vector-store-chunks', async (_, { docId, fileName, chunks }: {
  docId: string; fileName: string; chunks: { index: number; content: string }[]
}) => {
  try {
    await storeChunks(docId, fileName, chunks);
    return { success: true };
  } catch (error: any) {
    console.error('[vector-store-chunks] 错误:', error);
    return { success: false, error: error.message };
  }
});

ipcMain.handle('vector-delete-chunks', async (_, docId: string) => {
  try {
    await deleteChunks(docId);
    return { success: true };
  } catch (error: any) {
    console.error('[vector-delete-chunks] 错误:', error);
    return { success: false, error: error.message };
  }
});

ipcMain.handle('vector-search', async (_, { queryText, topK }: { queryText: string; topK?: number }) => {
  try {
    const results = await vectorSearch(queryText, topK ?? 5);
    return { success: true, results };
  } catch (error: any) {
    console.error('[vector-search] 错误:', error);
    return { success: false, results: [], error: error.message };
  }
});

ipcMain.handle('vector-clear-all', async () => {
  try {
    await clearAll();
    return { success: true };
  } catch (error: any) {
    console.error('[vector-clear-all] 错误:', error);
    return { success: false, error: error.message };
  }
});

ipcMain.handle('vector-get-stats', async () => {
  try {
    const stats = await getStats();
    return { success: true, ...stats };
  } catch (error: any) {
    console.error('[vector-get-stats] 错误:', error);
    return { success: false, totalChunks: 0, totalDocs: 0, totalSummaries: 0, error: error.message };
  }
});

// ============================================================
// Summary Generation IPC Handlers (AgentRouter)
// ============================================================

ipcMain.handle('generate-summary', async (_, params: {
  docId: string;
  fileName: string;
  fullText: string;
  chunkIds: string[];
  llmConfig: AgentConfig;
}) => {
  try {
    const { docId, fileName, fullText, chunkIds, llmConfig } = params;

    // 标记为 pending
    try { await updateSummaryStatus(docId, 'pending'); } catch {}

    // 通过 AgentRouter 调用 IndexAgent 生成摘要
    const summary = await agentRouter.dispatch('index', fullText, { llmConfig });

    // 存储摘要
    await storeSummary({ docId, fileName, summary, chunkIds });

    console.log(`[generate-summary] summary generated for ${fileName}`);
    return { success: true, summary };
  } catch (error: any) {
    console.error('[generate-summary] 错误:', error);
    // 标记为 failed
    try { await updateSummaryStatus(params.docId, 'failed'); } catch {}
    return { success: false, error: error.message };
  }
});

ipcMain.handle('retry-summary', async (_, params: {
  docId: string;
  fileName: string;
  fullText: string;
  chunkIds: string[];
  llmConfig: AgentConfig;
}) => {
  try {
    const { docId, fileName, fullText, chunkIds, llmConfig } = params;

    // 只删除旧摘要（保留 chunks）
    await deleteSummaryOnly(docId);

    const summary = await agentRouter.dispatch('index', fullText, { llmConfig });
    await storeSummary({ docId, fileName, summary, chunkIds });

    console.log(`[retry-summary] summary regenerated for ${fileName}`);
    return { success: true, summary };
  } catch (error: any) {
    console.error('[retry-summary] 错误:', error);
    try { await updateSummaryStatus(params.docId, 'failed'); } catch {}
    return { success: false, error: error.message };
  }
});

ipcMain.handle('get-summary-status', async (_, docIds: string[]) => {
  try {
    const statuses: Record<string, { exists: boolean; status?: string; summary?: string }> = {};
    for (const docId of docIds) {
      statuses[docId] = await getSummaryStatus(docId);
    }
    return { success: true, statuses };
  } catch (error: any) {
    console.error('[get-summary-status] 错误:', error);
    return { success: false, error: error.message };
  }
});

// ============================================================
// Intent Vector Database IPC Handlers
// ============================================================

ipcMain.handle('intent-init', async () => {
  try {
    await initIntentTable();
    return { success: true };
  } catch (error: any) {
    console.error('[intent-init] 错误:', error);
    return { success: false, error: error.message };
  }
});

ipcMain.handle('intent-index', async (_, rules: { id: string; text: string; ruleId: string }[]) => {
  try {
    await storeIntents(rules);
    return { success: true };
  } catch (error: any) {
    console.error('[intent-index] 错误:', error);
    return { success: false, error: error.message };
  }
});

ipcMain.handle('intent-patch', async (_, { toDelete, toAdd }: {
  toDelete: string[];
  toAdd: { id: string; text: string; ruleId: string }[];
}) => {
  try {
    await patchIntents(toDelete, toAdd);
    return { success: true };
  } catch (error: any) {
    console.error('[intent-patch] 错误:', error);
    return { success: false, error: error.message };
  }
});

ipcMain.handle('intent-search', async (_, { queryText, topK }: {
  queryText: string;
  topK?: number;
}) => {
  try {
    const results = await searchIntents(queryText, topK ?? 3);
    return { success: true, results };
  } catch (error: any) {
    console.error('[intent-search] 错误:', error);
    return { success: false, results: [], error: error.message };
  }
});