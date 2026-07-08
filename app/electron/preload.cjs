/**
 * Preload script (CommonJS)
 * Runs before the renderer process, has access to ipcRenderer
 */

const { ipcRenderer } = require('electron');

function domReady(condition) {
  if (condition === void 0) { condition = ['complete', 'interactive']; }
  return new Promise(function (resolve) {
    if (condition.includes(document.readyState)) {
      resolve(true);
    } else {
      document.addEventListener('readystatechange', function () {
        if (condition.includes(document.readyState)) {
          resolve(true);
        }
      });
    }
  });
}

var safeDOM = {
  append: function (parent, child) {
    if (!Array.from(parent.children).find(function (e) { return e === child; })) {
      return parent.appendChild(child);
    }
  },
  remove: function (parent, child) {
    if (Array.from(parent.children).find(function (e) { return e === child; })) {
      return parent.removeChild(child);
    }
  },
};

function useLoading() {
  var className = 'loaders-css__square-spin';
  var styleContent = [
    '@keyframes square-spin {',
    '  25% { transform: perspective(100px) rotateX(180deg) rotateY(0); }',
    '  50% { transform: perspective(100px) rotateX(180deg) rotateY(180deg); }',
    '  75% { transform: perspective(100px) rotateX(0) rotateY(180deg); }',
    '  100% { transform: perspective(100px) rotateX(0) rotateY(0); }',
    '}',
    '.' + className + ' > div {',
    '  animation-fill-mode: both;',
    '  width: 50px;',
    '  height: 50px;',
    '  background: #fff;',
    '  animation: square-spin 3s 0s cubic-bezier(0.09, 0.57, 0.49, 0.9) infinite;',
    '}',
    '.app-loading-wrap {',
    '  position: fixed;',
    '  top: 0;',
    '  left: 0;',
    '  width: 100vw;',
    '  height: 100vh;',
    '  display: flex;',
    '  align-items: center;',
    '  justify-content: center;',
    '  background: #282c34;',
    '  z-index: 9;',
    '}',
  ].join('\n');

  var oStyle = document.createElement('style');
  var oDiv = document.createElement('div');
  oStyle.id = 'app-loading-style';
  oStyle.innerHTML = styleContent;
  oDiv.className = 'app-loading-wrap';
  oDiv.innerHTML = '<div class="' + className + '"><div></div></div>';

  return {
    appendLoading: function () {
      safeDOM.append(document.head, oStyle);
      safeDOM.append(document.body, oDiv);
    },
    removeLoading: function () {
      safeDOM.remove(document.head, oStyle);
      safeDOM.remove(document.body, oDiv);
    },
  };
}

var loading = useLoading();
var appendLoading = loading.appendLoading;
var removeLoading = loading.removeLoading;

domReady().then(appendLoading);

var appLoaded = false;
var timedOut = false;

setTimeout(function () {
  timedOut = true;
  if (appLoaded) removeLoading();
}, 4999);

window.onmessage = function (event) {
  if (event.data.payload === 'removeLoading') {
    appLoaded = true;
    if (timedOut) removeLoading();
  }
  console.info(event.data.payload);
};

/** Forward model load status from Main to Renderer */
ipcRenderer.on('model-load-status', function (_event, status) {
  window.postMessage({ payload: 'model-load-status', data: status }, '*');
});

/** Skills Knowledge Base API */
window.skillsAPI = {
  list: function () { return ipcRenderer.invoke('skills-list'); },
  read: function (skillId) { return ipcRenderer.invoke('skills-read', skillId); },
};
