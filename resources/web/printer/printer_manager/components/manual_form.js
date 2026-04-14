const ManualFormTemplate = /*html*/
    `
        <el-form
            ref="manualForm"
            class="manual-form"
            :model="formData"
            :rules="formRules"
            label-position="top"
            autocomplete="off"
        >
            <div class="form-group-box">
                <div class="form-group">
                    <div class="form-group-header">
                        <label class="required">{{ $t('manualForm.printerModel') }}</label>
                        <div class="form-group-advanced">
                            <span>{{ $t('manualForm.advanced') }}</span>
                            <label class="toggle-switch">
                                <input type="checkbox" v-model="showAdvanced" @change="toggleAdvanced"/>
                                <span class="toggle-slider"></span>
                            </label>
                        </div>
                    </div>
                    <div class="form-group-row">
                        <div class="form-group-col">
                            <el-form-item prop="vendor">
                                <el-select
                                    :show-arrow="false"
                                    v-model="formData.vendor"
                                    @change="onVendorChange"
                                    :disabled="isVendorDisabled"
                                    :placeholder="$t('manualForm.selectVendor')"
                                >
                                    <el-option
                                        v-for="vendor in vendors"
                                        :key="vendor"
                                        :value="vendor"
                                        :label="vendor"
                                    />
                                </el-select>
                            </el-form-item>
                        </div>
                        <div class="form-group-col">
                            <el-form-item prop="printerModel">
                                <el-select
                                    :show-arrow="false"
                                    v-model="formData.printerModel"
                                    @change="onModelChange"
                                    :disabled="isModelDisabled"
                                    :placeholder="$t('manualForm.selectModel')"
                                >
                                    <el-option
                                        v-for="model in availableModels"
                                        :key="model.modelName"
                                        :value="model.modelName"
                                        :label="model.modelName"
                                        :data-host-type="model.hostType"
                                    />
                                </el-select>
                            </el-form-item>
                        </div>
                    </div>
                </div>

                <div class="form-group">
                    <label class="required">{{ $t('manualForm.printerName') }}</label>
                    <el-form-item prop="printerName">
                        <el-input
                            type="text"
                            v-model="formData.printerName"
                            :placeholder="formData.printerModel"
                            maxlength="30"
                            required
                            @keydown="onPrinterNameKeydown"
                        />
                    </el-form-item>
                </div>

                <div class="form-group" v-if="showHostType">
                    <label class="required">{{ $t('manualForm.hostType') }}</label>
                    <el-form-item prop="hostType">
                        <el-select
                            v-model="formData.hostType"
                            :placeholder="$t('manualForm.selectHostType')"
                            :show-arrow="false"
                            :disabled="isHostTypeDisabled"
                        >
                            <el-option
                                v-for="hostType in hostTypes"
                                :key="hostType"
                                :value="hostType"
                                :label="hostType"
                            />
                        </el-select>
                    </el-form-item>
                </div>

                <div class="form-group" v-if="showNetworkType">
                    <label class="required">{{ $t('manualForm.connectionMethod') }}</label>
                    <el-form-item prop="networkType">
                        <el-select
                            :show-arrow="false"
                            v-model="formData.networkType"
                            @change="onNetworkTypeChange"
                            :disabled="isNetworkTypeDisabled"
                            :placeholder="$t('manualForm.selectConnectionMethod')"
                        >
                            <el-option
                                v-for="option in networkTypeOptions"
                                :key="option.value"
                                :value="option.value"
                                :label="$t(option.label)"
                            />
                        </el-select>
                    </el-form-item>
                </div>

                <div class="form-group" v-if="formData.networkType === 0">
                    <label class="required">{{ $t('manualForm.hostNameIpUrl') }}</label>
                    <el-form-item prop="host">
                        <el-input
                            type="text"
                            v-model="formData.host"
                            required
                            @keydown="onHostKeydown"
                            :title="$t('manualForm.hostSamples')"
                        />
                    </el-form-item>
                </div>

                <div class="form-group">
                    <label :class="{ required: formData.networkType === 1 }">{{ formData.networkType === 0 ? $t('manualForm.accessCode') : $t('manualForm.pinCode') }}</label>
                    <template v-if="formData.networkType === 0">
                        <el-form-item prop="accessCode">
                            <el-input
                                type="password"
                                v-model="formData.accessCode"
                                :placeholder="$t('manualForm.accessCodePlaceholder')"
                                maxlength="20"
                                show-password
                            />
                        </el-form-item>
                    </template>
                    <template v-else>
                        <el-form-item prop="pinCode">
                            <el-input
                                type="password"
                                v-model="formData.pinCode"
                                :placeholder="$t('manualForm.pinCodePlaceholderRequired')"
                                maxlength="6"
                                show-password
                            />
                        </el-form-item>
                    </template>
                </div>

                
                <div class="form-group advanced-field" v-show="showAdvanced">
                    <label>{{ $t('manualForm.webUrl') }}</label>
                    <el-form-item prop="webUrl">
                        <el-input 
                        type="text" 
                        v-model="formData.webUrl"
                        @keydown="onWebUrlKeydown"
                        />
                    </el-form-item>
                </div>
                
                <div class="form-group advanced-field" v-show="showAdvanced">
                    <label>{{ $t('manualForm.password') }}</label>
                    <el-input 
                    type="password" 
                    v-model="formData.password"                 
                    @keydown="onPasswordKeydown"
                    />
                </div>
                
                <div class="form-group advanced-field" v-show="showAdvanced">
                    <label>{{ $t('manualForm.httpsCAFile') }}</label>
                    <div class="input-group">
                        <el-input type="text" v-model="formData.caFile" readonly/>
                        <button type="button" class="btn-secondary">{{ $t('manualForm.preview') }}</button>
                    </div>
                </div>
                
                <div class="form-group advanced-field" v-show="showAdvanced">
                    <label class="checkbox-label">
                        <span>{{ $t('manualForm.ignoreCertRevocation') }}</span>
                        <input type="checkbox" v-model="formData.ignoreCertRevocation"/>
                    </label>
                </div>
                
                <div class="form-group advanced-field" v-show="showAdvanced">
                    <label class="form-note">{{ $t('manualForm.httpsCAFileNote') }}</label>
                </div>
            </div>
        </el-form>
    `;


// Manual Form Component (can be imported by other applications)
const ManualFormComponent = {
    template: ManualFormTemplate,
    setup() {
        const printerStore = usePrinterStore();
        return {
            printerStore
        };
    },
    props: {
        printer: {
            type: Object,
            default: null
        }
    },

    emits: [],

    data() {
        return {
            // Priority printer models list (in order)
            priorityModels: [
                'Elegoo Centauri Carbon 2',
                'Elegoo Centauri Carbon',
                'Elegoo Centauri',
                'Elegoo OrangeStorm Giga',
                'Elegoo Neptune 4 Max',
                'Elegoo Neptune 4 Plus',
                'Elegoo Neptune 4 Pro',
                'Elegoo Neptune 4',
                'Elegoo Neptune 3 Max',
                'Elegoo Neptune 3 Plus',
                'Elegoo Neptune 3 Pro',
                'Elegoo Neptune 3',
                'Elegoo Neptune 2S',
                'Elegoo Neptune 2D',
                'Elegoo Neptune 2',
                'Elegoo Neptune',
                'Elegoo Neptune X'
            ],
            formData: {
                vendor: '',
                printerModel: '',
                printerName: '',
                hostType: '',
                networkType: 0,
                host: '',
                serialNumber: '',
                accessCode: '',
                pinCode: '',
                webUrl: '',
                password: '',
                caFile: '',
                ignoreCertRevocation: false
            },
            formRules: {
                vendor: [
                    { required: true, message: this.$t('manualForm.pleaseSelectVendor'), trigger: 'change' }
                ],
                printerModel: [
                    { required: true, message: this.$t('manualForm.pleaseSelectPrinterModel'), trigger: 'change' }
                ],
                printerName: [
                    // { required: true, message: this.$t('manualForm.pleaseEnterPrinterName'), trigger: 'change' },
                    {
                        validator: (rule, value, callback) => {
                            const trimmedValue = value.trim();
                            // Allow empty (will use model as default name)
                            if (trimmedValue.length === 0) {
                                callback();
                                return;
                            }
                            // Check length (1-30 characters)
                            if (trimmedValue.length > 30) {
                                callback(new Error(this.$t('manualForm.lengthShouldBe1To30')));
                                return;
                            }
                            // Support all languages and specified special characters (including spaces)
                            const nameRegex = /^[\p{L}\p{N} ・!@#$‰^&*()_+=\-`~\[\]{};:'",.\\s<>/?.|]+$/u;
                            if (!nameRegex.test(trimmedValue)) {
                                callback(new Error(this.$t('manualForm.invalidPrinterName')));
                                return;
                            }
                            callback();
                        },
                        trigger: 'change'
                    }
                ],
                hostType: [
                    {
                        validator: (rule, value, callback) => {
                            if (this.showHostType && !value) {
                                callback(new Error(this.$t('manualForm.pleaseSelectHostType')));
                                return;
                            }
                            callback();
                        },
                        trigger: 'change'
                    }
                ],
                networkType: [
                    {
                        validator: (rule, value, callback) => {
                            if (!this.showNetworkType) {
                                callback();
                                return;
                            }
                            if (value === null || value === undefined) {
                                callback(new Error(this.$t('manualForm.pleaseSelectConnectionMethod')));
                                return;
                            }
                            callback();
                        },
                        trigger: 'change'
                    }
                ],
                host: [
                    {
                        validator: (rule, value, callback) => {
                            if (this.formData.networkType !== 0) {
                                callback();
                                return;
                            }
                            if (!value || !value.trim()) {
                                callback(new Error(this.$t('manualForm.pleaseEnterHostNameIpUrl')));
                                return;
                            }
                            this.printerStore.validateHost(rule, value, callback);
                        },
                        trigger: 'blur'
                    }
                ],
                pinCode: [
                    {
                        validator: (rule, value, callback) => {
                            if (this.formData.networkType !== 1) {
                                callback();
                                return;
                            }
                            if (!value || !value.trim()) {
                                callback(new Error(this.$t('manualForm.pleaseEnterPinCode')));
                                return;
                            }
                            if (value.trim().length < 6) {
                                callback(new Error(this.$t('manualForm.pinCodeMustBe6Characters')));
                                return;
                            }
                            callback();
                        },
                        trigger: 'blur'
                    }
                ]
            },
            showAdvanced: false,
            vendors: [],
            availableModels: [],
            hostTypes: [],
            allModels: [],
            isVendorDisabled: false,
            isModelDisabled: false,
            isHostTypeDisabled: false,
            isNetworkTypeDisabled: false,
            showHostType: true,
            networkTypeOptions: [
                { value: 0, label: 'manualForm.networkTypeLan' },
                { value: 1, label: 'manualForm.networkTypeWan' }
            ],
            wanCapabilityMap: {}
        };
    },

    mounted() {
    },

    beforeUnmount() {
    },

    computed: {
        printerModelList() {
            return this.printerStore.printerModelList;
        },
        isEditing() {
            return !!this.printer;
        },
        showNetworkType() {
            if (this.isEditing && this.formData.networkType === 1) {
                return true;
            }
            return this.isWanCapableModel(this.formData.vendor, this.formData.printerModel);
        }
    },

    watch: {
        printerModelList: {
            handler(newVal) {
                if (newVal) {
                    this.renderPrinterModelList(newVal, this.printer);
                }
            },
            immediate: true
        },

        printer: {
            handler(newVal) {
                if (newVal && this.printerModelList) {
                    this.renderPrinter(newVal);
                }
            },
            immediate: true
        }
    },

    methods: {
        renderPrinterModelList(printerModelList, printer = null) {

            if (!printerModelList) return;

            // extract vendor list
            this.vendors = printerModelList.map(vendorModels => vendorModels.vendor);

            // extract all models
            this.allModels = [];
            this.wanCapabilityMap = {};
            printerModelList.forEach(vendorModels => {
                const vendorName = vendorModels.vendor || '';
                const vendorLower = vendorName.toString().trim().toLowerCase();
                (vendorModels.models || []).forEach(model => {
                    const modelName = model.modelName || '';
                    const modelLower = modelName.toString().trim().toLowerCase();
                    this.allModels.push({
                        vendor: vendorName,
                        vendorLower,
                        modelName,
                        modelNameLower: modelLower,
                        hostType: model.hostType,
                        supportWanNetwork: !!model.supportWanNetwork
                    });
                    const key = `${vendorLower}|||${modelLower}`;
                    this.wanCapabilityMap[key] = !!model.supportWanNetwork;
                });
            });

            // extract unique host types
            const uniqueHostTypes = new Set();
            this.allModels.forEach(model => {
                if (model.hostType) {
                    uniqueHostTypes.add(model.hostType);
                }
            });
            this.hostTypes = Array.from(uniqueHostTypes);

            // if there is vendor, default select the first one
            if (this.vendors.length > 0 && !this.formData.vendor) {
                this.formData.vendor = this.vendors[0];
                this.onVendorChange();
            }

            // if there is printer data to fill, execute after model list rendering
            if (printer) {
                this.renderPrinter(printer);
            }
            this.ensureNetworkTypeState();
        },

        onVendorChange() {
            const selectedVendor = this.formData.vendor;
            this.availableModels = [];
            this.showHostType = this.shouldShowHostType(selectedVendor);

            if (selectedVendor && this.printerModelList) {
                const vendorData = this.printerModelList.find(vendor => vendor.vendor === selectedVendor);
                if (vendorData) {
                    const models = (vendorData.models || []).map(model => ({
                        ...model,
                        supportWanNetwork: !!model.supportWanNetwork
                    }));

                    // Sort models by priority
                    this.availableModels = this.sortModelsByPriority(models);

                    if (this.availableModels.length > 0 && !this.formData.printerModel) {
                        this.formData.printerModel = this.availableModels[0].modelName;
                        this.onModelChange();
                    }
                }
            }
            this.ensureNetworkTypeState();
        },

        onModelChange() {
            const selectedModel = this.formData.printerModel;
            if (selectedModel) {
                const model = this.availableModels.find(m => m.modelName === selectedModel);
                if (model) {
                    this.formData.hostType = model.hostType;
                    this.formData.supportWanNetwork = model.supportWanNetwork;
                }
            }
            this.ensureNetworkTypeState();
        },

        toggleAdvanced() {
            // showAdvanced is automatically updated through v-model
        },

        shouldShowHostType(vendor) {
            if (!vendor) {
                return true;
            }
            return vendor.toString().toLowerCase() !== 'elegoo';
        },

        isWanCapableModel(vendor, model) {
            if (!vendor || !model) {
                return false;
            }
            const normalizedVendor = vendor.toString().trim().toLowerCase();
            const normalizedModel = model.toString().trim().toLowerCase();
            const key = `${normalizedVendor}|||${normalizedModel}`;
            if (Object.prototype.hasOwnProperty.call(this.wanCapabilityMap, key)) {
                return !!this.wanCapabilityMap[key];
            }
            return false;
        },

        sortModelsByPriority(models) {
            if (!models || models.length === 0) {
                return [];
            }

            const prioritySet = new Set(this.priorityModels.map(m => m.toLowerCase()));
            const priorityModels = [];
            const otherModels = [];

            models.forEach(model => {
                const modelName = model.modelName || '';
                if (prioritySet.has(modelName.toLowerCase())) {
                    priorityModels.push(model);
                } else {
                    otherModels.push(model);
                }
            });

            // Sort priority models according to the order in priorityModels list
            priorityModels.sort((a, b) => {
                const indexA = this.priorityModels.findIndex(m => m.toLowerCase() === (a.modelName || '').toLowerCase());
                const indexB = this.priorityModels.findIndex(m => m.toLowerCase() === (b.modelName || '').toLowerCase());
                return indexA - indexB;
            });

            return [...priorityModels, ...otherModels];
        },

        ensureNetworkTypeState() {
            const wanSupported = this.isWanCapableModel(this.formData.vendor, this.formData.printerModel);
            if (!wanSupported) {
                if (this.isEditing && this.formData.networkType === 1) {
                    return;
                }
                if (this.formData.networkType !== 0) {
                    this.formData.networkType = 0;
                }
                if (this.formData.pinCode) {
                    this.formData.pinCode = '';
                }
                this.$nextTick(() => {
                    if (this.$refs.manualForm) {
                        this.$refs.manualForm.clearValidate(['networkType', 'pinCode']);
                    }
                });
            } else if (this.formData.networkType !== 0 && this.formData.networkType !== 1) {
                this.formData.networkType = 0;
            }
        },

        onNetworkTypeChange() {
            if (this.formData.networkType === 0) {
                this.formData.pinCode = '';
            } else {
                this.formData.accessCode = '';
                this.formData.host = '';
            }
            this.$nextTick(() => {
                if (this.$refs.manualForm) {
                    this.$refs.manualForm.clearValidate(['host', 'pinCode']);
                }
            });
            this.ensureNetworkTypeState();
        },

        onPrinterNameKeydown(e) {
            if (e.keyCode === 13) { // Enter key
                e.preventDefault();
                this.$refs.manualForm.validateField('printerName');
            }
        },

        onHostKeydown(e) {
            if (e.keyCode === 13) { // Enter key
                e.preventDefault();
                this.$refs.manualForm.validateField('host');
            }
        },

        onWebUrlKeydown(e) {
            if (e.keyCode === 13) {
                e.preventDefault();
                this.$refs.manualForm.validateField('webUrl');
            }
        },

        onPasswordKeydown(e) {
            if (e.keyCode === 13) {
                e.preventDefault();
                this.$refs.manualForm.validateField('password');
            }
        },

        renderPrinter(printer) {
            if (!printer) return;

            // handle vendor and model selection
            if (printer.vendor && printer.vendor !== this.formData.vendor) {
                this.formData.vendor = printer.vendor;
                this.onVendorChange();
            }

            if (printer.printerModel && printer.printerModel !== this.formData.printerModel) {
                this.formData.printerModel = printer.printerModel;
                this.onModelChange();
            }

            if (printer.hostType && printer.hostType !== this.formData.hostType) {
                this.formData.hostType = printer.hostType;
            }

            // if it is an existing printer, disable vendor and model selection
            if (printer.vendor || printer.printerModel) {
                this.isVendorDisabled = true;
                this.isModelDisabled = true;
                this.isHostTypeDisabled = true;
                this.isNetworkTypeDisabled = true;
            }

            // fill other fields
            this.formData.printerName = printer.printerName ? printer.printerName : '';
            this.formData.host = printer.host ? printer.host : '';
            this.showHostType = this.shouldShowHostType(printer.vendor);
            this.formData.serialNumber = printer.serialNumber ? printer.serialNumber : '';
            this.formData.networkType = typeof printer.networkType === 'number' ? printer.networkType : 0;
            if (this.formData.networkType === 0) {
                this.formData.accessCode = printer.accessCode ? printer.accessCode : '';
                this.formData.pinCode = '';
            } else {
                this.formData.pinCode = printer.pinCode ? printer.pinCode : '';
                this.formData.accessCode = '';
            }
            this.formData.webUrl = printer.webUrl ? printer.webUrl : '';
            this.formData.password = printer.password ? printer.password : '';

            let extraInfo = {};
            if (printer.extraInfo) {
                if (typeof printer.extraInfo === 'string') {
                    try {
                        extraInfo = JSON.parse(printer.extraInfo);
                    } catch (error) {
                        extraInfo = {};
                    }
                } else {
                    extraInfo = printer.extraInfo;
                }
            }
            this.formData.caFile = extraInfo.caFile ? extraInfo.caFile : '';
            this.formData.ignoreCertRevocation = extraInfo.ignoreCertRevocation ? extraInfo.ignoreCertRevocation : false;
            this.ensureNetworkTypeState();
        },

        getFormData() {
            return new Promise((resolve) => {
                this.$refs.manualForm.validate((valid) => {
                    if (valid) {
                        const printerName = this.formData.printerName.trim() ? this.formData.printerName.trim() : this.formData.printerModel;
                        const host = this.formData.host ? this.formData.host.trim() : '';
                        const webUrl = this.formData.webUrl ? this.formData.webUrl.trim() : '';
                        const serialNumber = this.formData.serialNumber ? this.formData.serialNumber.trim() : '';
                        const networkType = this.formData.networkType;
                        const accessCode = networkType === 0 && this.formData.accessCode ? this.formData.accessCode.trim() : '';
                        const pinCode = networkType === 1 && this.formData.pinCode ? this.formData.pinCode.trim() : '';
                        const password = this.formData.password ? this.formData.password.trim() : '';
                        const extraInfo = {
                            caFile: this.formData.caFile,
                            ignoreCertRevocation: this.formData.ignoreCertRevocation
                        };
                        const printer = {
                            printerName,
                            host,
                            vendor: this.formData.vendor,
                            printerModel: this.formData.printerModel,
                            hostType: this.formData.hostType,
                            networkType,
                            webUrl,
                            serialNumber,
                            accessCode,
                            pinCode,
                            password,
                            extraInfo: JSON.stringify(extraInfo),
                        };
                        resolve(printer);
                    } else {
                        resolve(null);
                    }
                });
            });
        },

        // add form reset method
        resetForm() {
            this.$refs.manualForm.resetFields();
            this.showAdvanced = false;
            this.isVendorDisabled = false;
            this.isModelDisabled = false;
            this.isHostTypeDisabled = false;
            this.isNetworkTypeDisabled = false;
            this.showHostType = true;
            this.formData.networkType = 0;
            this.formData.serialNumber = '';
            this.formData.accessCode = '';
            this.formData.pinCode = '';
            this.formData.webUrl = '';
            this.formData.password = '';
            this.formData.caFile = '';
            this.formData.ignoreCertRevocation = false;
            this.ensureNetworkTypeState();
        },

        // add form validation method
        validateForm() {
            return new Promise((resolve) => {
                this.$refs.manualForm.validate((valid) => {
                    resolve(valid);
                });
            });
        }
    }
};
