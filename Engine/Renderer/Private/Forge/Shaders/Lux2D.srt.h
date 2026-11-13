#ifndef LUX2D_SRT_H
#define LUX2D_SRT_H

// Descriptor layout shared by the LuxForge shaders and LuxForge.cpp.
BEGIN_SRT(Lux2DSrt)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(LuxLights), gLights)
    END_SRT_SET(PerFrame)
    BEGIN_SRT_SET(PerBatch)
        DECL_TEXTURE(PerBatch, Tex2D(float4), gTexture)
    END_SRT_SET(PerBatch)
    BEGIN_SRT_SET(PerDraw)
        DECL_CBUFFER(PerDraw, CBUFFER(LuxDraw), gDraw)
    END_SRT_SET(PerDraw)
END_SRT(Lux2DSrt)

#endif
