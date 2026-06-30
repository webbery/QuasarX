<template>
    <div class="input-group">
            <label for="sync-path">数据文件夹路径</label>
            <input
                type="text"
                placeholder="请选择或输入要保存数据的文件夹路径"
                v-model="selectedFolderPath"
                readonly
            >
        </div>

        <div class="button-group">
            <button class="selection" @click="onHandleSelection">
                <i class="fas fa-folder-open"></i>选择本地数据文件夹
            </button>
            <button class="btn" @click="onHandleDownload">
                <i class="fas fa-sync-alt"></i>同步数据
            </button>
        </div>

        <div class="status">
            <div class="status-title">
                <i class="fas fa-info-circle"></i>同步状态
            </div>
            <div class="status-content">
                最近同步: {{ lastSyncDate }}<br>
                状态: <span style="color: #4ade80;">{{ status }}</span>
            </div>
        </div>

        <!-- Tick 数据下载 -->
        <div class="section-divider"></div>
        <div class="section-title">
            <i class="fas fa-chart-bar"></i> Tick 数据下载 (DuckDB)
        </div>

        <div class="input-group">
            <label>本地 DuckDB 路径</label>
            <input
                type="text"
                placeholder="如留空，默认在数据文件夹下创建 tick_data.db"
                v-model="tickDbPath"
            >
        </div>

        <div class="input-group">
            <label>标的代码 (留空下载全部)</label>
            <input
                type="text"
                placeholder="如 600000.SH"
                v-model="tickSymbol"
            >
        </div>

        <div class="input-row">
            <div class="input-group">
                <label>开始日期</label>
                <input type="date" v-model="tickStartDate" />
            </div>
            <div class="input-group">
                <label>结束日期</label>
                <input type="date" v-model="tickEndDate" />
            </div>
        </div>

        <div class="button-row">
            <button class="btn" @click="onHandleTickDownload" :disabled="tickDownloading">
                <i class="fas fa-download"></i>
                {{ tickDownloading ? '下载中...' : '下载 Tick 到 DuckDB' }}
            </button>
            <span v-if="tickDownloading && tickProgress > 0" class="progress-text">
                已下载 {{ tickCount }} 条
            </span>
        </div>

        <div v-if="tickDownloadStatus" class="status-text" :class="{ 'status-error': tickDownloadStatus.includes('失败') }">
            {{ tickDownloadStatus }}
        </div>

        <!-- 服务端数据管理 -->
        <div class="section-divider danger"></div>
        <div class="section-title danger">
            <i class="fas fa-exclamation-triangle"></i> 服务端数据管理
        </div>

        <div class="button-row">
            <button class="btn-danger" @click="onHandleDeleteServerTicks" :disabled="deleting">
                <i class="fas fa-trash"></i>
                {{ deleting ? '删除中...' : '删除服务端 Tick 数据' }}
            </button>
        </div>

        <div v-if="deleteStatus" class="status-text" :class="{ 'status-error': deleteStatus.includes('失败') }">
            {{ deleteStatus }}
        </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { ipcRenderer } from 'electron'
import Store from 'electron-store';

let status = ref('')
const store = new Store();

const storeKey = 'syncPath'
let selectedFolderPath = ref(store.get(storeKey) || '');
let lastSyncDate = ref(store.get('lastSyncDate') || '');

// Tick 下载状态
const tickDbPath = ref('')
const tickSymbol = ref('')
const tickStartDate = ref('')
const tickEndDate = ref('')
const tickDownloading = ref(false)
const tickProgress = ref(0)
const tickCount = ref(0)
const tickDownloadStatus = ref('')

// 删除状态
const deleting = ref(false)
const deleteStatus = ref('')

// 监听进度事件
const onTickProgress = (_, data) => {
    tickCount.value = data.count
}

onMounted(() => {
    ipcRenderer.on('tick-download-progress', onTickProgress)
})

onUnmounted(() => {
    ipcRenderer.removeListener('tick-download-progress', onTickProgress)
})

const onHandleDownload = async () => {
    console.info("onHandleDownload", selectedFolderPath.value);
    const filePath = 'sync.zip'
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = 'https://' + server + '/v0/data/sync'
    const result = await ipcRenderer.invoke("merge-csv", url, token, filePath, selectedFolderPath.value);
    if (result === true) {
        const date = new Date()
        const year = date.getFullYear();
        const month = String(date.getMonth() + 1).padStart(2, '0');
        const day = String(date.getDate()).padStart(2, '0');
        const hours = String(date.getHours()).padStart(2, '0');
        const minutes = String(date.getMinutes()).padStart(2, '0');

        const formattedString = `${year}-${month}-${day} ${hours}:${minutes}`;
        lastSyncDate.value = formattedString
        store.set('lastSyncDate', formattedString)
        status.value = '同步成功'
    } else {
        status.value = '同步失败'
    }
}

const onHandleSelection = async () => {
  console.info("onHandleSelection");
  try {
    const result = await ipcRenderer.invoke("open-directory-dialog");
    if (!result.canceled) {
        selectedFolderPath.value = result;
        store.set(storeKey, result)
    }
  } catch (err) {
    console.error('Error selecting folder:', err);
  }
}

const onHandleTickDownload = async () => {
    if (!selectedFolderPath.value) {
        tickDownloadStatus.value = '请先选择本地数据文件夹'
        return
    }

    tickDownloading.value = true
    tickCount.value = 0
    tickDownloadStatus.value = '正在连接服务端...'

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = `https://${server}/v0/replay`
    const startTs = tickStartDate.value ? Math.floor(new Date(tickStartDate.value).getTime() / 1000) : 0
    const endTs = tickEndDate.value ? Math.floor(new Date(tickEndDate.value + 'T23:59:59').getTime() / 1000) : 0

    // 默认 DB 路径
    const dbPath = tickDbPath.value || `${selectedFolderPath.value}/tick_data.db`

    try {
        const result = await ipcRenderer.invoke('tick-sync-to-duckdb',
            url, token, tickSymbol.value || null, startTs, endTs, dbPath)

        if (result.success) {
            tickDownloadStatus.value = `下载完成，共 ${result.count} 条 tick → ${dbPath}`
        } else {
            tickDownloadStatus.value = `下载失败: ${result.error}`
        }
    } catch (err) {
        tickDownloadStatus.value = `下载失败: ${err.message}`
    } finally {
        tickDownloading.value = false
    }
}

const onHandleDeleteServerTicks = async () => {
    const result = await ipcRenderer.invoke('show-message-box', {
        type: 'warning',
        title: '确认删除',
        message: '确定要删除服务端所有 Tick 数据吗？此操作不可恢复！',
        buttons: ['取消', '确定删除'],
        defaultId: 0,
        cancelId: 0
    })

    if (!result || result.response !== 1) return

    deleting.value = true
    deleteStatus.value = '正在删除...'

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = `https://${server}/v0/replay`

    try {
        // before=0 表示删除全部数据
        const res = await ipcRenderer.invoke('delete-server-ticks', url, token, 0)
        if (res.deleted) {
            deleteStatus.value = '删除成功'
        } else if (res.error) {
            deleteStatus.value = `删除失败: ${res.error}`
        } else {
            deleteStatus.value = '删除完成'
        }
    } catch (err) {
        deleteStatus.value = `删除失败: ${err.message}`
    } finally {
        deleting.value = false
    }
}
</script>

<style scoped>
input {
    width: 100%;
    padding: 6px 8px;
    background: rgba(15, 23, 42, 0.7);
    border: 1px solid rgba(74, 158, 255, 0.3);
    border-radius: 8px;
    color: #e2e8f0;
    font-size: 1rem;
}
input:focus {
    outline: none;
    border-color: #4a9eff;
    box-shadow: 0 0 0 2px rgba(74, 158, 255, 0.2);
}
.selection {
    border: none;
    padding: 8px 25px;
    border-radius: 8px;
    font-weight: 600;
    cursor: pointer;
    background: rgba(30, 41, 59, 0.8);
    color: #e2e8f0;
}
.btn {
    background: linear-gradient(90deg, #2563eb, #1d4ed8);
    color: white;
    border: none;
    padding: 12px 25px;
    border-radius: 8px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.3s ease;
    align-items: center;
    gap: 8px;
    box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}
.btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}
.btn-danger {
    background: linear-gradient(90deg, #dc2626, #b91c1c);
    color: white;
    border: none;
    padding: 10px 20px;
    border-radius: 8px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.3s ease;
}
.btn-danger:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}
.input-row {
    display: flex;
    gap: 16px;
    margin-top: 12px;
}
.input-row .input-group {
    flex: 1;
}
.button-group {
    display: flex;
    gap: 12px;
    margin-top: 12px;
}
.button-row {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-top: 12px;
}
.progress-text {
    color: #94a3b8;
    font-size: 0.9rem;
}
.status-text {
    margin-top: 8px;
    color: #94a3b8;
    font-size: 0.9rem;
}
.status-text.status-error {
    color: #f87171;
}
.section-divider {
    height: 1px;
    background: rgba(74, 158, 255, 0.2);
    margin: 20px 0;
}
.section-divider.danger {
    background: rgba(248, 113, 113, 0.3);
}
.section-title {
    color: #e2e8f0;
    font-size: 1rem;
    font-weight: 600;
    margin-bottom: 12px;
    display: flex;
    align-items: center;
    gap: 8px;
}
.section-title.danger {
    color: #f87171;
}
.input-group {
    margin-top: 12px;
}
.input-group label {
    display: block;
    color: #94a3b8;
    font-size: 0.85rem;
    margin-bottom: 4px;
}
</style>
