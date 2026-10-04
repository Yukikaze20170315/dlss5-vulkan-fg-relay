option casemap:none
EXTERN SrFixtureBody:PROC
.code
NVSDK_NGX_D3D12_EvaluateFeature PROC
    mov [rsp+8], rbx
    jmp SrFixtureBody
NVSDK_NGX_D3D12_EvaluateFeature ENDP
END
