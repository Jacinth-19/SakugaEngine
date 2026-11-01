Shader "Custom/EndfieldDebug"
{
    Properties
    {
        _MainTex ("Texture", 2D) = "white" {}
        _Channel ("Channel 0=RGB 1=R 2=G 3=B 4=A", Range(0, 4)) = 0
        _Exposure ("Exposure", Range(0, 8)) = 1
    }

    SubShader
    {
        Tags
        {
            "RenderPipeline" = "UniversalPipeline"
            "RenderType" = "Opaque"
            "Queue" = "Geometry"
        }

        Pass
        {
            Name "Debug"
            Tags { "LightMode" = "UniversalForward" }

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag

            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            struct Attributes
            {
                float4 positionOS : POSITION;
                float2 uv : TEXCOORD0;
            };

            struct Varyings
            {
                float4 positionCS : SV_POSITION;
                float2 uv : TEXCOORD0;
            };

            CBUFFER_START(UnityPerMaterial)
                float4 _MainTex_ST;
                float _Channel;
                float _Exposure;
            CBUFFER_END

            TEXTURE2D(_MainTex); SAMPLER(sampler_MainTex);

            Varyings vert(Attributes input)
            {
                Varyings output;
                output.positionCS = TransformObjectToHClip(input.positionOS.xyz);
                output.uv = TRANSFORM_TEX(input.uv, _MainTex);
                return output;
            }

            float4 frag(Varyings input) : SV_Target
            {
                float4 tex = SAMPLE_TEXTURE2D(_MainTex, sampler_MainTex, input.uv);
                float3 rgb = tex.rgb;
                rgb = lerp(rgb, tex.rrr, step(0.5, _Channel) * (1.0 - step(1.5, _Channel)));
                rgb = lerp(rgb, tex.ggg, step(1.5, _Channel) * (1.0 - step(2.5, _Channel)));
                rgb = lerp(rgb, tex.bbb, step(2.5, _Channel) * (1.0 - step(3.5, _Channel)));
                rgb = lerp(rgb, tex.aaa, step(3.5, _Channel));
                return float4(saturate(rgb * _Exposure), 1);
            }
            ENDHLSL
        }
    }
}
