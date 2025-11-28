<template>
    <transition name="modal-fade">
        <div v-if="showLoginModal" class="modal-overlay" @click.self="closeModal">
          <div class="login-modal">
            <div class="modal-header">
              <h2>用户登录</h2>
              <button class="close-btn" @click="closeModal">x</button>
            </div>
            
            <form @submit.prevent="handleLogin" class="login-form">
              <div class="form-group horizontal">
                <label for="username">用户名</label>
                <div class="input-container">
                  <input
                    type="text"
                    id="username"
                    v-model.trim="loginForm.username"
                    placeholder="请输入用户名"
                    :class="{ 'is-invalid': errors.username }"
                  >
                  <div class="invalid-feedback" v-if="errors.username">
                    {{ errors.username }}
                  </div>
                </div>
              </div>
              
              <div class="form-group horizontal">
                <label for="password">密码</label>
                <div class="input-container">
                  <input
                    type="password"
                    id="password"
                    v-model.trim="loginForm.password"
                    placeholder="请输入密码"
                    :class="{ 'is-invalid': errors.password }"
                  >
                  <div class="invalid-feedback" v-if="errors.password">
                    {{ errors.password }}
                  </div>
                </div>
              </div>
              
              <div class="form-group horizontal">
                <label for="server">远程服务器</label>
                <div class="input-container">
                  <div class="custom-select">
                    <input
                      :value="selectedServer ? selectedServer.label : ''"
                      readonly
                      @click="toggleDropdown"
                      placeholder="请选择服务器IP"
                      class="select-input"
                    >
                    <div v-show="showDropdown" class="dropdown-options">
                      <div
                        v-for="server in serverOptions"
                        :key="server.value"
                        @click="selectServer(server)"
                        class="dropdown-option"
                      >
                        {{ server.label }}
                      </div>
                    </div>
                  </div>
                </div>
              </div>
              
              <div class="form-group horizontal">
                <label></label>
                <div class="input-container">
                  <label class="remember-me">
                    <input type="checkbox" v-model="rememberMe"> 记住登录信息
                  </label>
                </div>
              </div>
              <div class="form-actions">
                <button 
                  type="submit" 
                  class="login-btn"
                  :disabled="isSubmitting"
                >
                  {{ isSubmitting ? '登录中...' : '登录' }}
                </button>
              </div>
            </form>
          </div>
        </div>
      </transition>
</template>
<script setup>
import axios from 'axios'
import { ref, reactive, onMounted, onUnmounted, defineEmits, defineProps } from 'vue'
import Store from 'electron-store';
import { message } from '@/tool';
import sseService from '@/ts/SSEService';

const store = new Store();
const emit = defineEmits(['onStatusChange', 'closeLoginForm'])
const props = defineProps({
  showLoginModal: Boolean,
})
// 表单数据
const loginForm = reactive({
  username: '',
  password: '',
  server: ''
})

// 服务器选项
const serverOptions = ref([])

// 下拉菜单状态
const showDropdown = ref(false)
const selectedServer = ref(null)

// 表单错误信息
const errors = reactive({
  username: '',
  password: ''
})

// 提交状态
const isSubmitting = ref(false)

// 切换下拉菜单显示
const toggleDropdown = () => {
  showDropdown.value = !showDropdown.value
}

// 选择服务器
const selectServer = (server) => {
  selectedServer.value = server
  loginForm.server = server.value
  showDropdown.value = false
}

// 点击外部关闭下拉菜单
const handleClickOutside = (e) => {
  const selectElements = document.querySelectorAll('.custom-select, .select-input, .dropdown-options, .dropdown-option')
  const isClickInside = Array.from(selectElements).some(element => 
    element.contains(e.target)
  )
  
  if (!isClickInside) {
    showDropdown.value = false
  }
}

// 保存登录信息到localStorage
const saveLoginInfo = () => {
  if (rememberMe.value) {
    const loginInfo = {
      username: loginForm.username,
      password: loginForm.password, // 注意：实际项目中密码应该加密存储
      server: loginForm.server,
      serverLabel: selectedServer.value ? selectedServer.value.label : '',
      rememberMe: true
    }
    localStorage.setItem('savedLoginInfo', JSON.stringify(loginInfo))
  } else {
    // 如果不记住，清除保存的信息
    localStorage.removeItem('savedLoginInfo')
  }
}

// 从localStorage加载保存的登录信息
const loadSavedLoginInfo = () => {
  const savedInfo = localStorage.getItem('savedLoginInfo')
  if (savedInfo) {
    try {
      const loginInfo = JSON.parse(savedInfo)
      loginForm.username = loginInfo.username || ''
      loginForm.password = loginInfo.password || ''
      loginForm.server = loginInfo.server || ''
      
      // 查找对应的服务器选项
      if (loginInfo.server && serverOptions.value.length > 0) {
        const server = serverOptions.value.find(s => s.value === loginInfo.server)
        if (server) {
          selectedServer.value = server
        }
      }
      
      rememberMe.value = loginInfo.rememberMe || false
    } catch (error) {
      console.error('加载保存的登录信息失败:', error)
      // 如果解析失败，清除损坏的数据
      localStorage.removeItem('savedLoginInfo')
    }
  }
}

onMounted(() => {
  const data = store.get('servers')
  for (const item of data) {
    console.info(item)
    serverOptions.value.push({
      name: item.name,
      label: item.name,
      value: item.address + ':19107'
    })
  }

  // 加载保存的登录信息
  loadSavedLoginInfo()

  document.addEventListener('click', handleClickOutside)
})

onUnmounted(() => {
  document.removeEventListener('click', handleClickOutside)
})

// 关闭模态框
const closeModal = () => {
    emit('closeLoginForm')
};

// 表单验证
const validateForm = () => {
    let isValid = true;
    errors.username = '';
    errors.password = '';
    
    if (!loginForm.username.trim()) {
        errors.username = '请输入用户名';
        isValid = false;
    }
    
    if (!loginForm.password) {
        errors.password = '请输入密码';
        isValid = false;
    }
    
    return isValid;
};

// 处理登录
const handleLogin = () => {
    isSubmitting.value = true;
    if (validateForm()) {
        const fs = require('fs')
        const certificate = fs.readFileSync('src/assets/server.crt', 'utf-8')
        const https = require('https');
        const httpsAgent = new https.Agent({
          rejectUnauthorized: false, // 如果是自签名证书，需要设置这个选项为 false
          // cert: certificate, // 证书文件
        })
        let url = 'https://'+ loginForm.server + '/v0/user/login'
        fetch(url, {
          method: 'POST',
          headers: {
            'Content-Type': 'application/json'
          },
          body: JSON.stringify({
            'name': loginForm.username,
            'pwd': loginForm.password
          })
        })
        .then(async response => {
          if (response.ok) {
            let data = await response.json()
            console.info(data)
            let mode = data['mode']
            let token = data['tk']
            localStorage.setItem('token', token)
            localStorage.setItem('remote', loginForm.server)

            // 保存登录信息
            saveLoginInfo()
            
            // 设置拦截器
            axios.interceptors.request.use((config) => {
              if (token) {
                config.headers.Authorization = token
              }
              return config
            })
            axios.defaults.baseURL = 'https://' + loginForm.server
            axios.defaults.headers.common['Authorization'] = token
            const agent = new https.Agent({  
                rejectUnauthorized: false // 忽略证书错误
            });
            axios.defaults.httpsAgent = agent

            // 登录成功后触发账户数据刷新
            window.dispatchEvent(new Event('loginSuccess'))
            sseService.connect(token)
            let removeInfo = selectedServer.value.label
            if (mode === 'Backtest') {
              emit('onStatusChange', true, removeInfo + ' - 回测模式')
            }
            else if (mode === 'Simulation') {
              emit('onStatusChange', true, removeInfo + ' - 模拟盘模式')
            }
            else if (mode === 'Real') {
              emit('onStatusChange', true, removeInfo + ' - 实盘模式')
            }
            isSubmitting.value = false;
            closeModal();
          } else {
            message.error(`登录失败`)
          }
        })
        .catch(error => {
          message.error(`登录失败: ${error}`)
        }).finally(()=>{
          isSubmitting.value = false;
        })
    } else {
      isSubmitting.value = false;
    }
};
</script>
<style scoped>
/* 模态框遮罩层 */
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-color: rgba(0, 0, 0, 0.5);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 1000;
}
/* 登录模态框 */
.login-modal {
  background: var(--panel-bg);
border-radius: 12px;
box-shadow: 0 4px 20px rgba(0, 0, 0, 0.5);
width: 90%;
max-width: 500px;
animation: modal-appear 0.3s ease-out;
border: 1px solid var(--border);
}
@keyframes modal-appear {
  from {
    opacity: 0;
    transform: translateY(-30px) scale(0.95);
  }
  to {
    opacity: 1;
    transform: translateY(0) scale(1);
  }
}

/* 模态框头部 */
.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px 24px;
  border-bottom: 1px solid var(--border);
background-color: var(--darker-bg);
}

.modal-header h2 {
  margin: 0;
  color: var(--text);
  font-size: 1.5rem;
}

.close-btn {
  background: none;
  border: none;
  font-size: 24px;
  cursor: pointer;
  color: var(--text-secondary);
  padding: 0;
  width: 30px;
  height: 30px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 50%;
}

.close-btn:hover {
  background-color: rgba(255, 255, 255, 0.1);
    color: var(--text);
}

/* 登录表单 */
.login-form {
  padding: 24px;
}
.form-group {
  margin-bottom: 20px;
}

.form-group.horizontal {
  display: flex;
  align-items: center;
}

.form-group label {
  display: block;
  color: var(--text);
  font-weight: 500;
  width: 100px;
  flex-shrink: 0;
  margin-right: 15px;
  text-align: right;
}

.input-container {
  flex: 1;
  position: relative;
}

.form-group input {
  width: 100%;
  padding: 12px 16px;
  background-color: var(--darker-bg);
color: var(--text);
border: 1px solid var(--border);
  font-size: 16px;
  box-sizing: border-box;
 transition: all 0.3s;
}

.form-group input:focus {
  outline: none;
  border-color: var(--primary);
box-shadow: 0 0 0 3px rgba(41, 98, 255, 0.2);
}

.is-invalid {
  border-color: #f87171 !important;
}

.invalid-feedback {
  color: #f87171;
  font-size: 14px;
  margin-top: 6px;
  position: absolute;
  bottom: -20px;
  left: 0;
}

/* 自定义下拉选择 */
.custom-select {
  position: relative;
  width: 100%;
}

.select-input {
  cursor: pointer;
  background-color: var(--darker-bg);
color: var(--text); 
}

.dropdown-options {
  position: absolute;
  top: 100%;
  left: 0;
  right: 0;
  margin-top: 4px;
  border: 1px solid var(--border);
border-radius: 8px;
background: var(--panel-bg);
box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
  z-index: 10;
  max-height: 200px;
  overflow-y: auto;
}

.dropdown-option {
  padding: 12px 16px;
  cursor: pointer;
   transition: background-color 0.2s;
border-bottom: 1px solid var(--border);
color: var(--text);
  white-space: normal;
  word-wrap: break-word;
}

.dropdown-option:last-child {
  border-bottom: none;
}

.dropdown-option:hover {
  background-color: rgba(255, 255, 255, 0.05);
}

/* 表单操作 */
.form-actions {
  margin-top: 24px;
  padding-left: 115px;
}

.login-btn {
  width: 100%;
  padding: 14px;
  background-color: var(--primary);
  color: white;
  border: none;
  border-radius: 8px;
  font-size: 16px;
  font-weight: 600;
  cursor: pointer;
  transition: background-color 0.3s;
}

.login-btn:hover:not(:disabled) {
  background-color: #2049d4;
}

.login-btn:disabled {
  background-color: #2a3449;
  color: var(--text-secondary);
  cursor: not-allowed;
}

/* 模态框动画 */
.modal-fade-enter-active,
.modal-fade-leave-active {
  transition: opacity 0.3s ease;
}

.modal-fade-enter-from,
.modal-fade-leave-to {
  opacity: 0;
}
</style>