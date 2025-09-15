<template>
    <div class="input-group">
            <label for="sync-path">同步文件夹路径</label>
            <input
                type="text" 
                placeholder="请选择或输入要同步的文件夹路径"
                v-model="selectedFolderPath"
                readonly
            >
        </div>
        
        <div class="button-group">
            <button class="selection" @click="onHandleSelection">
                <i class="fas fa-folder-open"></i>选择本地同步文件夹
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
        
        <!-- <div class="stats">
            <div class="stat-card">
                <div class="stat-value">2.5GB</div>
                <div class="stat-label">已同步数据</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">184</div>
                <div class="stat-label">文件数量</div>
            </div>
            <div class="stat-card">
                <div class="stat-value">98%</div>
                <div class="stat-label">可用空间</div>
            </div>
        </div> -->
</template>
<script setup>
import {ref, onMounted} from 'vue'
import {ipcRenderer} from 'electron'
import axios from 'axios'
import Store from 'electron-store';

let status = ref('')
const store = new Store();

const storeKey = 'syncPath'
let selectedFolderPath = ref(store.get(storeKey));
let lastSyncDate = ref(store.get('lastSyncDate'));



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
        const month = String(date.getMonth() + 1).padStart(2, '0'); // 月份从0开始，需要+1:cite[1]
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
        // console.log('Selected folder path:', result);
        selectedFolderPath.value = result;
        store.set(storeKey, result)
    } else {
        // cancel
    }
  } catch (err) {
    console.error('Error selecting folder:', err);
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
.selection {
    border: none;
  padding: 8px 25px;
  border-radius: 8px;
  font-weight: 600;
  cursor: pointer;
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
</style>