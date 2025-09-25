<template>
    <div class="server-card" v-for="server in servers" :key="server.name">
        <div class="server-header">
            <div class="server-icon">
                <i class="fas fa-server"></i>
            </div>
            <div class="server-info">
                <h3>{{ server.name }}</h3>
            </div>
        </div>
        
        <div class="server-details">
            <div class="detail-row">
                <span class="detail-label">地址:</span>
                <span class="detail-value">{{ server.address }}</span>
            </div>
            <div class="detail-row">
                <span class="detail-label">名称:</span>
                <span class="detail-value"  @dblclick="startEditing(server, 'name')" v-if="">
                    {{ server.name }}
                </span>
                <div v-else style="display: flex; align-items: center; width: 100%;">
                    <input 
                        type="text" 
                        class="detail-input" 
                        v-model="server.editingValue"
                        @keyup.enter="saveEditing(server)"
                        @keyup.esc="cancelEditing(server)"
                        v-focus
                    >
                </div>
            <div class="detail-row">
                <span class="detail-label">密码:</span>
                <span class="detail-value">••••••••</span>
            </div>
        </div>
        
        <div class="server-actions">
            <button class="btn btn-edit" @click="editServer(server)">
                <i class="fas fa-key"></i> 修改密码
            </button>
            <button class="btn btn-delete" @click="deleteServer(server.name)">
                <i class="fas fa-trash"></i> 删除
            </button>
        </div>
    </div>
</template>
<script setup>
import Store from 'electron-store';
import { ref, computed } from 'vue'

const store = new Store();
let servers = ref(store.get('servers'))

const deleteServer = (severName) => {
    for (let item in servers.value) {
        
    }
}
</script>
<style scoped>
.server-details {
    margin: 15px 0;
    padding: 15px;
    background-color: var(--darker-bg);
    border-radius: 8px;
    border: 1px solid var(--border);
}

.detail-row {
    display: flex;
    margin-bottom: 8px;
}

.detail-label {
    font-weight: 500;
    width: 100px;
    color: var(--text-secondary);
}

.detail-value {
    flex: 1;
    color: var(--text);
}

.server-actions {
    display: flex;
    justify-content: flex-end;
    gap: 10px;
    margin-top: 15px;
}
</style>