import { createApp } from 'vue'
import ElementPlus from 'element-plus'
import * as ElementPlusIconsVue from '@element-plus/icons-vue'
import 'element-plus/dist/index.css'
import App from './App.vue'
import axios from 'axios'

axios.defaults.headers.common['Access-Control-Allow-Origin'] = process.env.VUE_APP_Access_Control_Allow_Origin
axios.create({
    baseURL: '/v0',
    timeout: 1000
})
const app = createApp(App)
    .use(ElementPlus)
    .mount('#app')
    .$nextTick(() => postMessage({ payload: 'removeLoading' }, '*') );
// for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
//     app.component(key, component)
// }
