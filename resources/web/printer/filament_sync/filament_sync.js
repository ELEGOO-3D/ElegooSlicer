// Vue.js filament sync application
const { createApp } = Vue;
const { ElButton, ElDialog, ElInput, ElSelect, ElOption, ElForm, ElFormItem, ElLoading } = ElementPlus;

const FilamentSyncApp = {
    data() {
        return {
            curPrinter: null,
            printerList: [],
            mmsInfo: null,
            printFilamentList: [],
            isIniting: false
        };
    },

    methods: {
        getFilamentSvg(filament) {
            const color = (filament && filament.filamentColor) || '#888';
            return getFilamentSvg(color);
        },

        // IPC Communication methods
        async ipcRequest(method, params = {}, timeout = 10000) {
            try {
                const response = await nativeIpc.request(method, params, timeout);
                return response;
            } catch (error) {
                let message = '';
                if (method === "syncMmsFilament") {
                    message = i18n.global.t("filamentSync.failedToSyncFilament");
                } else {
                    message = `${error.message || 'Unknown error occurred'}`;
                }
                // Show error notification using Element Plus message component
                if (window.ElementPlus && window.ElementPlus.ElMessage) {
                    window.ElementPlus.ElMessage.error({
                        message: message,
                        duration: 5000,
                        showClose: true
                    });
                }
                throw error;
            }
        },

        // Lifecycle methods
        async init() {
            this.isIniting = true;
            await this.requestPrinterList();
            this.isIniting = false;
        },

        // Communication with backend
        async requestPrinterList() {
            try {
                const response = await this.ipcRequest('getPrinterList', {});
                this.printerList = response || [];
                this.updatePrinterSelection();
            } catch (error) {
                console.error('Failed to request printer list:', error);
            }
        },

        // Request printer filament info when printer is selected
        async requestPrinterFilamentInfo(printerId) {
            const loading = ElLoading.service({
                lock: true,
            });
            try {
                const params = { printerId: printerId };
                const response = await this.ipcRequest('getPrinterFilamentInfo', params);
                this.mmsInfo = response.mmsInfo;
                this.printFilamentList = response.printFilamentList;
            } catch (error) {
                console.error('Failed to request printer filament info:', error);
                this.mmsInfo = null;
                this.printFilamentList = [];
            } finally {
                loading.close();
            }
        },

        async updatePrinterSelection() {
            if (this.printerList.length === 0) return;
            let selectedPrinter;
            if (this.curPrinter && this.curPrinter.printerId) {
                // find the printer by printerId
                let cur = this.printerList.find(p => p.printerId === this.curPrinter.printerId);
                if (cur) {
                    selectedPrinter = cur;
                }
            }
            // if not found, find the selected printer
            if (!selectedPrinter) {
                selectedPrinter = this.printerList.find(p => p.selected);
                if (!selectedPrinter) {
                    selectedPrinter = this.printerList[0];
                }
            }
            this.curPrinter = selectedPrinter;
            // Request filament info for the selected printer
            if (this.curPrinter && this.curPrinter.printerId) {
                await this.requestPrinterFilamentInfo(this.curPrinter.printerId);
            }
        },


        // Handle printer selection change
        async onPrinterChanged(printer) {
            this.curPrinter = printer;
            if (printer && printer.printerId) {
                await this.requestPrinterFilamentInfo(printer.printerId);
            }
        },

        async refreshPrinterList(event, device) {
            event.stopPropagation();
            event.preventDefault();
            await this.requestPrinterList();
        },

        async cancel() {
            try {
                nativeIpc.sendEvent('closeDialog', {});
            } catch (error) {
                console.error('Failed to close dialog:', error);
            }
        },

        async sync() {
            try {
                const params = {
                    mmsInfo: this.mmsInfo,
                    printFilamentList: this.printFilamentList,
                    printer: this.curPrinter
                };
                nativeIpc.sendEvent('syncMmsFilament', params);
            } catch (error) {
                console.error('Failed to sync filament:', error);
            }
        },

        // Printer status display methods
        getPrinterStatus(printerStatus, connectStatus) {
            return PrinterStatusUtils.getPrinterStatus(printerStatus, connectStatus, this.$t);
        },

        getPrinterStatusStyle(printerStatus, connectStatus) {
            return PrinterStatusUtils.getPrinterStatusStyle(printerStatus, connectStatus);
        },

        // Helper method to check if MMS has active filaments
        hasActiveFilaments() {
            // Check if printer and MMS info are available
            if (!this.curPrinter || !this.mmsInfo) {
                return false;
            }

            // Check if MMS list exists and has items
            const mmsList = this.mmsInfo.mmsList;
            if (!mmsList || mmsList.length === 0) {
                return false;
            }

            // Check if first MMS has tray list with items
            const firstMms = mmsList[0];
            if (!firstMms || !firstMms.trayList || firstMms.trayList.length === 0) {
                return false;
            }

            // Check if there are filaments with active status (1: loaded, 3: ready)
            const activeFilaments = firstMms.trayList.filter(tray =>
                tray.status === 1 || tray.status === 3
            );

            return activeFilaments.length > 0;
        }
    },

    // Initialize the application
    mounted() {
        // Initialize the application
        this.init();
        disableRightClickMenu();
    },
    computed: {
        networkPrinters() {
            return this.printerList.filter(printer => printer.networkType === 1);
        },
        localPrinters() {
            return this.printerList.filter(printer => printer.networkType === 0);
        },
        canSync() {
            // Check if printer list is available
            if (!this.printerList || this.printerList.length === 0) {
                return false;
            }

            // Use shared logic to check for active filaments
            return this.hasActiveFilaments();
        },
        hasFilament() {
            // Delegate to shared helper method
            return this.hasActiveFilaments();
        }
    }
};

// Create and mount the Vue app
const app = createApp(FilamentSyncApp)
    .use(ElementPlus)
    .use(i18n)
    .mount('#app');
