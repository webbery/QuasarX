/**
 * JavaScript 执行 Tool（合并了原来的 calculator / datetime / platform）
 *
 * 为什么合并：
 * 三个窄工具（计算器 / 时间 / 系统信息）各自带一份固定 schema，
 * 但模型真正需要的是「现场算一段逻辑」。一个能跑脚本的工具覆盖全部场景，
 * 而且以后新增计算需求不必再加工具。
 *
 * 沙箱边界（重要）：
 * 脚本跑在**独立 Worker** 里，不在渲染进程的主上下文 ——
 * 渲染进程开着 nodeIntegration + contextIsolation: false，
 * 在那里执行模型生成的代码等于把整台机器交出去。
 * Worker 里没有 Node、没有 DOM、没有文件、没有网络（见下方 BLOCKED 列表）。
 *
 * 能力边界：
 * - 只做「算」和「看环境」：不提供汇率表（汇率必须来自实时行情/联网，不能写死），
 *   也不提供期权收益计算（那属于期权定价域，由 option_pricing 工具负责）
 */

import { tool } from "@langchain/core/tools"
import { z } from "zod"

/** 默认超时 */
const DEFAULT_TIMEOUT_MS = 5000
/** 超时上限（防止模型申请一个跑几分钟的脚本） */
const MAX_TIMEOUT_MS = 15000

/**
 * Worker 引导源码
 *
 * `__SCRIPT__` 会被替换成模型给的脚本吗？—— 不会：脚本通过 postMessage 传进来，
 * 这里只有引导逻辑。引导做三件事：
 * 1. 切断逃逸面（网络 / 存储 / DOM / Node）
 * 2. 接管 postMessage（用户脚本不能自己伪造结果）
 * 3. 把脚本包成一个 async 函数体执行（支持顶层 await 与 return）
 */
export const WORKER_BOOTSTRAP = `
"use strict";
(function () {
  var nativePost = self.postMessage.bind(self);
  // ---- 切断逃逸面 ----
  // Worker 里本来就没有 Node/DOM，但网络与存储是有的（fetch / indexedDB），
  // 一并屏蔽：计算工具不需要出网。
  var BLOCKED = [
    "fetch", "XMLHttpRequest", "WebSocket", "EventSource", "importScripts",
    "indexedDB", "caches", "Worker", "SharedWorker",
    "navigator", "location", "crypto", "Notification", "BroadcastChannel",
    "RTCPeerConnection", "localStorage", "sessionStorage",
    "chrome", "process", "require", "module"
  ];
  for (var i = 0; i < BLOCKED.length; i++) {
    var key = BLOCKED[i];
    try { delete globalThis[key]; } catch (e) {}
    try { Object.defineProperty(globalThis, key, { value: undefined, configurable: true }); } catch (e) {}
  }
  // 用户脚本不能自己 postMessage 伪造结果
  try { Object.defineProperty(self, "postMessage", { value: function () {}, configurable: true }); } catch (e) {}

  self.onmessage = function (ev) {
    var data = ev.data || {};
    var logs = [];

    function stringify(v) {
      try {
        var s = JSON.stringify(v);
        return s === undefined ? String(v) : s;
      } catch (e) {
        return String(v);
      }
    }

    function print() {
      var parts = [];
      for (var i = 0; i < arguments.length; i++) {
        var a = arguments[i];
        parts.push(typeof a === "string" ? a : stringify(a));
      }
      logs.push(parts.join(" "));
    }

    function fail(e) {
      nativePost({ ok: false, error: (e && e.message) ? e.message : String(e), logs: logs });
    }

    try {
      var fn = new Function(
        "print", "system", "now",
        '"use strict";\\nreturn (async () => {\\n' + String(data.script || "") + '\\n})();'
      );
      Promise.resolve(fn(print, data.system, Date.now())).then(
        function (v) { nativePost({ ok: true, value: stringify(v), logs: logs }); },
        fail
      );
    } catch (e) {
      fail(e);
    }
  };
})();
`

/**
 * 采集系统信息快照（主线程取，注入沙箱）
 *
 * 只取「算题/排障用得上」的字段，不取主机名、用户名这类身份信息。
 */
function collectSystemInfo(): Record<string, unknown> {
  const info: Record<string, unknown> = {}

  try {
    const nav = typeof navigator !== "undefined" ? navigator : undefined
    if (nav) {
      info.platform = nav.platform
      info.language = nav.language
      info.languages = nav.languages
      info.hardwareConcurrency = nav.hardwareConcurrency
      const mem = (nav as unknown as { deviceMemory?: number }).deviceMemory
      if (mem) info.deviceMemoryGb = mem
    }
  } catch { /* 忽略：非浏览器环境 */ }

  try {
    info.timezone = Intl.DateTimeFormat().resolvedOptions().timeZone
    info.locale = Intl.DateTimeFormat().resolvedOptions().locale
  } catch { /* 忽略 */ }

  try {
    info.screen = {
      width: window.screen?.width,
      height: window.screen?.height,
      devicePixelRatio: window.devicePixelRatio,
    }
  } catch { /* 忽略 */ }

  try {
    info.appTitle = document.title
  } catch { /* 忽略 */ }

  try {
    // 渲染进程开着 nodeIntegration 时才有 process（Worker 里没有）
    const proc = (globalThis as unknown as { process?: any }).process
    if (proc?.versions?.node) {
      info.nodeVersion = proc.versions.node
      info.osPlatform = proc.platform
      info.arch = proc.arch
    }
  } catch { /* 忽略 */ }

  return info
}

/** 在沙箱里跑一段脚本 */
function runScript(
  script: string,
  timeoutMs: number,
): Promise<{ ok: boolean; value?: string; logs: string[]; error?: string }> {
  if (typeof Worker === "undefined") {
    return Promise.resolve({
      ok: false,
      logs: [],
      error: "当前环境不支持 Worker，无法执行脚本（该工具仅在 Electron 渲染进程可用）",
    })
  }

  let url: string
  try {
    url = URL.createObjectURL(new Blob([WORKER_BOOTSTRAP], { type: "application/javascript" }))
  } catch (e) {
    return Promise.resolve({
      ok: false,
      logs: [],
      error: `沙箱创建失败：${e instanceof Error ? e.message : String(e)}`,
    })
  }

  const worker = new Worker(url)

  return new Promise((resolve) => {
    let settled = false
    const finish = (result: { ok: boolean; value?: string; logs: string[]; error?: string }) => {
      if (settled) return
      settled = true
      clearTimeout(timer)
      try {
        worker.terminate()
      } catch { /* 忽略 */ }
      URL.revokeObjectURL(url)
      resolve(result)
    }

    const timer = setTimeout(() => {
      finish({ ok: false, logs: [], error: `脚本执行超时（${timeoutMs}ms），已终止` })
    }, timeoutMs)

    worker.onmessage = (ev: MessageEvent) => {
      const data = (ev.data ?? {}) as { ok?: boolean; value?: string; logs?: string[]; error?: string }
      finish({
        ok: !!data.ok,
        value: data.value,
        logs: Array.isArray(data.logs) ? data.logs : [],
        error: data.error,
      })
    }

    worker.onerror = (ev: ErrorEvent) => {
      finish({ ok: false, logs: [], error: ev?.message || "脚本执行失败" })
    }

    try {
      worker.postMessage({ script, system: collectSystemInfo() })
    } catch (e) {
      finish({ ok: false, logs: [], error: `脚本投递失败：${e instanceof Error ? e.message : String(e)}` })
    }
  })
}

export const javascriptTool = tool(
  async ({ script, timeout_ms }) => {
    if (!script?.trim()) {
      return "错误：需要 script 参数（要执行的 JavaScript 源码）"
    }

    const timeout = Math.min(
      Math.max(timeout_ms ?? DEFAULT_TIMEOUT_MS, 500),
      MAX_TIMEOUT_MS,
    )
    const result = await runScript(script, timeout)

    if (!result.ok) {
      const tail = result.logs.length ? `\n\n已输出：\n${result.logs.join("\n")}` : ""
      return `脚本执行失败：${result.error}${tail}`
    }

    const parts: string[] = []
    if (result.logs.length) parts.push(`【print 输出】\n${result.logs.join("\n")}`)
    if (result.value !== undefined && result.value !== "undefined") {
      parts.push(`【返回值】\n${result.value}`)
    }
    if (!parts.length) parts.push("脚本执行成功（没有输出）。")
    return parts.join("\n")
  },
  {
    name: "javascript",
    description:
      "在受限沙箱里执行 JavaScript，用于精确计算、日期时间与系统信息查询。\n" +
      "\n" +
      "沙箱边界：脚本跑在独立 Worker 里 —— 没有 DOM、没有文件、没有网络、没有 Node API，" +
      "只能用 JS 标准内建（Math / Date / JSON / Intl / Array …）。\n" +
      "\n" +
      "注入的变量：\n" +
      "- system：系统信息快照（platform / language / timezone / locale / screen / nodeVersion / cpu 核数 / 内存 …）\n" +
      "- now()：当前时间戳（毫秒）\n" +
      "- print(...)：输出内容（可多次，结果里会带上）\n" +
      "\n" +
      "写法：脚本是函数体，可以用 return 返回结果，顶层也支持 await。\n" +
      "示例：const xs=[1,2,3,4]; const mean=xs.reduce((a,b)=>a+b,0)/xs.length; " +
      "const sd=Math.sqrt(xs.reduce((a,b)=>a+(b-mean)**2,0)/xs.length); return {mean, sd}\n" +
      "\n" +
      "注意：\n" +
      "- 需要当前日期时间就用 new Date()（不要编造日期）\n" +
      "- 本工具不提供汇率表：汇率必须来自实时行情或联网结果，不能写死\n" +
      "- 期权定价/希腊/隐波属于 option_pricing 工具，组合收益计算也在那里，不要在这里手搓",
    schema: z.object({
      script: z
        .string()
        .describe("要执行的 JavaScript 源码。函数体形式：可用 return 返回结果，可用 print(...) 输出"),
      timeout_ms: z
        .number()
        .optional()
        .describe(`执行超时（毫秒），默认 ${DEFAULT_TIMEOUT_MS}，上限 ${MAX_TIMEOUT_MS}`),
    }),
  },
)
