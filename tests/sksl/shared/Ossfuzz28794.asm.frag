OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %sk_FragColor %sk_Clockwise
OpExecutionMode %main OriginUpperLeft
OpName %sk_FragColor "sk_FragColor"
OpName %sk_Clockwise "sk_Clockwise"
OpName %main "main"
OpName %i "i"
OpDecorate %sk_FragColor RelaxedPrecision
OpDecorate %sk_FragColor Location 0
OpDecorate %sk_FragColor Index 0
OpDecorate %sk_Clockwise RelaxedPrecision
OpDecorate %sk_Clockwise BuiltIn FrontFacing
%float = OpTypeFloat 32
%v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
%sk_FragColor = OpVariable %_ptr_Output_v4float Output
%bool = OpTypeBool
%_ptr_Input_bool = OpTypePointer Input %bool
%sk_Clockwise = OpVariable %_ptr_Input_bool Input
%void = OpTypeVoid
%11 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%float_1 = OpConstant %float 1
%int_3 = OpConstant %int 3
%v4int = OpTypeVector %int 4
%int_1 = OpConstant %int 1
%v2int = OpTypeVector %int 2
%_ptr_Output_float = OpTypePointer Output %float
%int_0 = OpConstant %int 0
%main = OpFunction %void None %11
%12 = OpLabel
%i = OpVariable %_ptr_Function_int Function
%16 = OpExtInst %float %1 Sqrt %float_1
%18 = OpConvertFToS %int %16
OpStore %i %18
%19 = OpLoad %int %i
OpStore %i %int_3
%21 = OpCompositeConstruct %v4int %int_3 %int_3 %int_3 %int_3
%23 = OpCompositeExtract %int %21 0
%25 = OpCompositeConstruct %v2int %23 %int_1
%27 = OpCompositeExtract %int %25 0
%28 = OpIMul %int %19 %27
%29 = OpLoad %int %i
%30 = OpConvertSToF %float %29
%31 = OpAccessChain %_ptr_Output_float %sk_FragColor %int_0
OpStore %31 %30
OpReturn
OpFunctionEnd
