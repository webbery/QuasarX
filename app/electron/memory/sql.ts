/**
 * SQL 字面量转义
 *
 * LanceDB 的查询条件是 SQL 字符串（DataFusion 语法），参数直接拼进 where 子句，
 * 所以拼之前必须转义单引号。
 */

/** 转义为 SQL 字符串字面量 */
export function sqlString(value: string): string {
  return `'${String(value ?? '').replace(/'/g, "''")}'`
}

/** 转义为 SQL 数字字面量 */
export function sqlNumber(value: number): string {
  return Number.isFinite(value) ? String(value) : 'NULL'
}
