file(READ "${TF}/Graphics/Direct3D12/Direct3D12.c" _forge_d3d12)

function(lux_patch_d3d12 original replacement)
    string(FIND "${_forge_d3d12}" "${original}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "The Forge D3D12 initialization patch does not match the source revision.")
    endif()
    string(REPLACE "${original}" "${replacement}" _forge_d3d12 "${_forge_d3d12}")
    set(_forge_d3d12 "${_forge_d3d12}" PARENT_SCOPE)
endfunction()

lux_patch_d3d12([[    for (UINT i = 0; DXGI_ERROR_NOT_FOUND != COM_CALL(EnumAdapterByGpuPreference, dxgiFactory, i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                      IID_REF(IDXGIAdapter4_Copy), &adapter);
         ++i)
    {
        if (gpuCount >= MAX_MULTIPLE_GPUS)
        {
            break;
        }]] [[    for (UINT i = 0; gpuCount < MAX_MULTIPLE_GPUS; ++i)
    {
        HRESULT result = COM_CALL(EnumAdapterByGpuPreference, dxgiFactory, i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                  IID_REF(IDXGIAdapter4_Copy), &adapter);
        if (result == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(result))
        {
            LOGF(eERROR, "DXGI adapter enumeration failed: HRESULT 0x%08X", (unsigned int)result);
            break;
        }]])

lux_patch_d3d12([[                if (SUCCEEDED(d3d12dll_CreateDevice(adapter, feature_levels[level], IID_REF(ID3D12Device), NULL)))]]
[[                HRESULT deviceResult = d3d12dll_CreateDevice(adapter, feature_levels[level], IID_REF(ID3D12Device), NULL);
                if (FAILED(deviceResult) && level + 1 == sizeof(feature_levels) / sizeof(feature_levels[0]))
                    LOGF(eERROR, "D3D12 device creation failed for %ls (vendor 0x%04X, device 0x%04X): HRESULT 0x%08X",
                         desc.Description, desc.VendorId, desc.DeviceId, (unsigned int)deviceResult);
                if (SUCCEEDED(deviceResult))]])

lux_patch_d3d12([[                            d3d12dll_CreateDevice(adapter, feature_levels[level], IID_ARGS(ID3D12Device, &device));
                            hook_fill_gpu_desc(device, feature_levels[level], pDXGPUInfo);]]
[[                            hres = d3d12dll_CreateDevice(adapter, feature_levels[level], IID_ARGS(ID3D12Device, &device));
                            if (FAILED(hres) || !device)
                            {
                                LOGF(eERROR, "D3D12 device creation failed for %ls: HRESULT 0x%08X",
                                     desc.Description, (unsigned int)hres);
                                SAFE_RELEASE(device);
                                SAFE_RELEASE(pDXGPUInfo->pGpu);
                                break;
                            }
                            hook_fill_gpu_desc(device, feature_levels[level], pDXGPUInfo);]])

lux_patch_d3d12([[        d3d12dll_CreateDevice(gpuDesc[i].pGpu, gpuDesc[i].mMaxSupportedFeatureLevel, IID_ARGS(ID3D12Device, &device));

        QueryGpuDesc(device, &gpuDesc[i], &pContext->mGpus[i]);]]
[[        HRESULT result = d3d12dll_CreateDevice(gpuDesc[i].pGpu, gpuDesc[i].mMaxSupportedFeatureLevel, IID_ARGS(ID3D12Device, &device));
        if (FAILED(result) || !device)
        {
            LOGF(eERROR, "D3D12 device creation failed while querying GPU capabilities: HRESULT 0x%08X", (unsigned int)result);
            SAFE_RELEASE(device);
            for (uint32_t j = 0; j < pContext->mGpuCount; ++j)
                SAFE_RELEASE(gpuDesc[j].pGpu);
            ExitCommon(pContext);
            SAFE_FREE(pContext);
            *ppContext = NULL;
            return;
        }

        QueryGpuDesc(device, &gpuDesc[i], &pContext->mGpus[i]);]])

lux_patch_d3d12([[    CHECK_HRESULT(d3d12dll_CreateDevice(pRenderer->pGpu->mDx.pGpu, supportedFeatureLevel, IID_ARGS(ID3D12Device, &pRenderer->mDx.pDevice)));]]
[[    HRESULT result = d3d12dll_CreateDevice(pRenderer->pGpu->mDx.pGpu, supportedFeatureLevel, IID_ARGS(ID3D12Device, &pRenderer->mDx.pDevice));
    if (FAILED(result) || !pRenderer->mDx.pDevice)
    {
        LOGF(eERROR, "D3D12 device creation failed for the renderer: HRESULT 0x%08X", (unsigned int)result);
        SAFE_RELEASE(pRenderer->mDx.pDevice);
        return false;
    }]])

lux_patch_d3d12([[    if (!pContext->mGpuCount && foundSoftwareAdapter)
    {
        LOGF(eERROR, "The only available GPU has DXGI_ADAPTER_FLAG_SOFTWARE. Early exiting");
        ASSERT(false);
        return;
    }

    ASSERT(pContext->mGpuCount);]] [[    if (!pContext->mGpuCount)
    {
        LOGF(eERROR, "No usable D3D12 hardware GPU was found. Software adapter present: %s",
             foundSoftwareAdapter ? "yes" : "no");
        ExitCommon(pContext);
        SAFE_FREE(pContext);
        *ppContext = NULL;
        return;
    }]])

lux_patch_d3d12([[    ASSERT(pContext->mGpuCount > 0);]] [[    if (!pContext->mGpuCount)
    {
        LOGF(eERROR, "D3D12 hardware GPUs became unavailable during initialization");
        ExitCommon(pContext);
        SAFE_FREE(pContext);
        *ppContext = NULL;
        return;
    }]])

lux_patch_d3d12([[        if (!AddDevice(pDesc, pRenderer))
        {
            *ppRenderer = NULL;
            return;
        }]] [[        if (!pRenderer->pContext || !AddDevice(pDesc, pRenderer))
        {
            if (pRenderer->mOwnsContext && pRenderer->pContext)
                exitRendererContext(pRenderer->pContext);
            SAFE_FREE(pRenderer);
            *ppRenderer = NULL;
            return;
        }]])

file(WRITE "${LUX_FORGE_CONFIG_DIR}/Direct3D12.c.tmp" "${_forge_d3d12}")
configure_file("${LUX_FORGE_CONFIG_DIR}/Direct3D12.c.tmp" "${LUX_FORGE_CONFIG_DIR}/Direct3D12.c" COPYONLY)
list(REMOVE_ITEM LUX_FORGE_SOURCES "${TF}/Graphics/Direct3D12/Direct3D12.c")
list(APPEND LUX_FORGE_SOURCES "${LUX_FORGE_CONFIG_DIR}/Direct3D12.c")
