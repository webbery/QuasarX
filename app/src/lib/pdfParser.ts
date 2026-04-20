/**
 * PDF 解析工具
 * 使用 pdf-parse-new 库解析 PDF 文本内容
 */

export interface PdfParseResult {
  text: string;        // 完整文本
  pages: number;       // 页数
  title: string;       // 提取的标题
  chunks: TextChunk[]; // 分块后的文本
  summary: string;     // 摘要
}

export interface TextChunk {
  index: number;
  content: string;
}

/**
 * PDF 解析配置
 */
interface ParseConfig {
  chunkSize?: number;    // 每块字符数，默认 500
  chunkOverlap?: number; // 重叠字符数，默认 50
  summaryLength?: number; // 摘要字符数，默认 300
}

/**
 * 从 ArrayBuffer 解析 PDF
 * @param fileData PDF 文件的 ArrayBuffer
 * @param config 解析配置
 * @returns PDF 解析结果
 */
export async function parsePdf(
  fileData: ArrayBuffer,
  config: ParseConfig = {}
): Promise<PdfParseResult> {
  const {
    chunkSize = 500,
    chunkOverlap = 50,
    summaryLength = 300,
  } = config;

  // 使用 pdf-parse-new 解析
  // 注意: pdf-parse-new 在 Electron 渲染器中需要通过主进程调用
  // 这里提供一个基于主进程的实现方案
  const text = await extractTextFromPdf(fileData);
  const pages = estimatePageCount(text);
  const title = extractTitle(text);
  const chunks = chunkText(text, chunkSize, chunkOverlap);
  const summary = generateSummary(text, summaryLength);

  return {
    text,
    pages,
    title,
    chunks: chunks.map((content, index) => ({ index, content })),
    summary,
  };
}

/**
 * 从 PDF ArrayBuffer 提取文本
 * 通过 Electron IPC 调用主进程的 pdf-parse
 */
async function extractTextFromPdf(fileData: ArrayBuffer): Promise<string> {
  const { ipcRenderer } = require('electron');

  try {
    const uint8Array = new Uint8Array(fileData);
    const result = await ipcRenderer.invoke(
      'parse-pdf',
      Array.from(uint8Array)
    );
    if (result.success) {
      return result.text;
    }
    console.warn('[pdfParser] 主进程解析失败');
    return '[PDF 解析失败]';
  } catch (e) {
    console.error('[pdfParser] IPC 调用失败:', e);
    return '[PDF 解析失败]';
  }
}

/**
 * 估算页数
 */
function estimatePageCount(text: string): number {
  // 粗略估计：每页约 3000 字符
  return Math.max(1, Math.ceil(text.length / 3000));
}

/**
 * 从文本中提取标题（取第一行非空文本）
 */
function extractTitle(text: string): string {
  const lines = text.split(/\r?\n/).filter(line => line.trim().length > 0);
  if (lines.length > 0) {
    return lines[0].substring(0, 100); // 限制标题长度
  }
  return '未命名文档';
}

/**
 * 文本分块
 */
function chunkText(text: string, chunkSize: number, overlap: number): string[] {
  const chunks: string[] = [];
  // 移除多余空白
  const cleaned = text.replace(/\s+/g, ' ').trim();

  for (let i = 0; i < cleaned.length; i += chunkSize - overlap) {
    const chunk = cleaned.substring(i, i + chunkSize);
    if (chunk.length > 0) {
      chunks.push(chunk);
    }
  }

  return chunks;
}

/**
 * 生成摘要（截取开头 + 关键词提取）
 */
function generateSummary(text: string, maxLength: number): string {
  const cleaned = text.replace(/\s+/g, ' ').trim();

  if (cleaned.length <= maxLength) {
    return cleaned;
  }

  // 简单策略：取前 N 个字符
  let summary = cleaned.substring(0, maxLength);

  // 确保不在单词中间截断
  const lastSpace = summary.lastIndexOf(' ');
  if (lastSpace > maxLength * 0.8) {
    summary = summary.substring(0, lastSpace);
  }

  return summary + '...';
}
