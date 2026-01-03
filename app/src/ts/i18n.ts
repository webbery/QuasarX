
const en2zh = {
    'query privilege fail': '查询交易权限失败'
}

function getZh(key: string): string {
    const k = key as keyof typeof en2zh;
    if (k in en2zh) {
        return en2zh[k];
    }
    return key
}
export default getZh