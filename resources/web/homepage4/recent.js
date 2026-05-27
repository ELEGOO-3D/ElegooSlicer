const { createApp } = Vue;
const { ElInput, ElButton, ElPopover, ElDialog, ElTab, ElTabPane, ElSelect, ElOption, ElForm, ElFormItem, ElLoading, ElMessageBox, ElPopconfirm } = ElementPlus;

const Recent = {
    data() {
        return {
            // {
            // image: '',
            // path: '',
            // project_name: '',
            // time: ''//"2025-10-24 10:28:50"
            // }
            recentFiles: [],
            selectedFile: null,
            selectedIndex: -1,
            sImages: {}
        };
    },
    methods: {
        updateRecentFiles(newFiles) {
            this.recentFiles = [];
            for (const file of newFiles) {
                file.image = file.image || this.sImages[file.path];
                this.sImages[file.path] = file.image;
                this.recentFiles.push(file);
            }
        },
        async init() {
            const rFiles = await this.ipcRequest('getRecentFiles', {});
            this.updateRecentFiles(rFiles);
        },

        async onClickNewProject() {
            console.log('New project clicked');
            await nativeIpc.sendEvent('createNewProject', {});
        },

        async onClickOpenProject() {
            console.log('Open project clicked');
            await nativeIpc.sendEvent('openProject', {});
        },

        async onOpenRecentFile(file) {
            console.log('Open recent file clicked');
            await nativeIpc.sendEvent('openRecentFile', { path: file.path });
        },
        async onDeleteAllRecentFiles() {
            console.log('Delete all recent files clicked');
            await this.ipcRequest('clearRecentFiles', {});
            await this.init();
        },

        onRightClickRecentFile(event, file, index) {
            console.log('Right click on file:', file);
            this.selectedFile = file;
            this.selectedIndex = index;

            const menu = document.getElementById('recent-context-menu');
            menu.style.display = 'block';
            menu.style.left = event.clientX + 'px';
            menu.style.top = event.clientY + 'px';
        },

        hideContextMenu() {
            const menu = document.getElementById('recent-context-menu');
            menu.style.display = 'none';
            this.selectedFile = null;
            this.selectedIndex = -1;
        },

        async onDeleteRecentFile() {
            if (!this.selectedFile) {
                console.log('No file selected');
                return;
            }

            console.log('Delete recent file clicked:', this.selectedFile);
            await this.ipcRequest('removeFromRecent', { path: this.selectedFile.path });
            await this.init();
            this.hideContextMenu();
        },

        async onExploreRecentFile() {
            if (!this.selectedFile) {
                console.log('No file selected');
                return;
            }

            console.log('Explore recent file clicked:', this.selectedFile);
            await nativeIpc.sendEvent('openFileInExplorer', { path: this.selectedFile.path });
            this.hideContextMenu();
        },

        // IPC Communication methods
        async ipcRequest(method, params = {}, timeout = 10000) {
            try {
                const response = await nativeIpc.request(method, params, timeout);
                return response;
            } catch (error) {
                console.error(`IPC request failed for ${method}:`, error);
                this.showStatusTip(error.message || 'Request failed');
                throw error;
            }
        },

        showStatusTip(message) {
            if (window.ElementPlus && window.ElementPlus.ElMessage) {
                window.ElementPlus.ElMessage.error({
                    message: message,
                    duration: 5000,
                    showClose: true
                });
            }
        },

        async sendMessage(command, data) {
            try {
                return await this.ipcRequest(command, data);
            } catch (error) {
                console.error('Send message failed:', error);
            }
        }
    },

    async mounted() {
        disableRightClickMenu();
        // console.log('Recent component mounted');
        this.init();

        nativeIpc.on('recentFilesUpdated', async (data) => {
            this.updateRecentFiles(data);
        });

        // Hide context menu when clicking elsewhere
        document.addEventListener('click', (e) => {
            const menu = document.getElementById('recent-context-menu');
            if (menu && !menu.contains(e.target)) {
                this.hideContextMenu();
            }
        });

        // // Prevent default context menu on the entire app
        // document.getElementById('app').addEventListener('contextmenu', (e) => {
        //     // Only prevent default if not right-clicking on a file card
        //     if (!e.target.closest('.file-card')) {
        //         e.preventDefault();
        //     }
        // });
    }
};

const app = createApp(Recent)
    .use(ElementPlus)
    .use(i18n)
    .mount('#app');
