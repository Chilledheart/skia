OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %_entrypoint "_entrypoint" %sk_FragColor %sk_Clockwise
OpExecutionMode %_entrypoint OriginUpperLeft
OpName %sk_FragColor "sk_FragColor"
OpName %sk_Clockwise "sk_Clockwise"
OpName %_UniformBuffer "_UniformBuffer"
OpMemberName %_UniformBuffer 0 "colorGreen"
OpName %_entrypoint "_entrypoint"
OpName %main "main"
OpName %_0_x "_0_x"
OpName %x "x"
OpName %_1_x "_1_x"
OpName %y "y"
OpDecorate %sk_FragColor RelaxedPrecision
OpDecorate %sk_FragColor Location 0
OpDecorate %sk_FragColor Index 0
OpDecorate %sk_Clockwise RelaxedPrecision
OpDecorate %sk_Clockwise BuiltIn FrontFacing
OpMemberDecorate %_UniformBuffer 0 Offset 0
OpMemberDecorate %_UniformBuffer 0 RelaxedPrecision
OpDecorate %_UniformBuffer Block
OpDecorate %10 Binding 0
OpDecorate %10 DescriptorSet 0
OpDecorate %58 RelaxedPrecision
%float = OpTypeFloat 32
%v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
%sk_FragColor = OpVariable %_ptr_Output_v4float Output
%bool = OpTypeBool
%_ptr_Input_bool = OpTypePointer Input %bool
%sk_Clockwise = OpVariable %_ptr_Input_bool Input
%_UniformBuffer = OpTypeStruct %v4float
%_ptr_Uniform__UniformBuffer = OpTypePointer Uniform %_UniformBuffer
%10 = OpVariable %_ptr_Uniform__UniformBuffer Uniform
%void = OpTypeVoid
%15 = OpTypeFunction %void
%18 = OpTypeFunction %v4float
%_ptr_Function_float = OpTypePointer Function %float
%float_1 = OpConstant %float 1
%float_2 = OpConstant %float 2
%v2float = OpTypeVector %float 2
%_ptr_Function_v2float = OpTypePointer Function %v2float
%37 = OpConstantComposite %v2float %float_1 %float_2
%float_3 = OpConstant %float 3
%float_4 = OpConstant %float 4
%45 = OpConstantComposite %v2float %float_3 %float_4
%_ptr_Uniform_v4float = OpTypePointer Uniform %v4float
%int = OpTypeInt 32 1
%int_0 = OpConstant %int 0
%_entrypoint = OpFunction %void None %15
%16 = OpLabel
%17 = OpFunctionCall %v4float %main
OpStore %sk_FragColor %17
OpReturn
OpFunctionEnd
%main = OpFunction %v4float None %18
%19 = OpLabel
%_0_x = OpVariable %_ptr_Function_float Function
%x = OpVariable %_ptr_Function_float Function
%_1_x = OpVariable %_ptr_Function_v2float Function
%y = OpVariable %_ptr_Function_v2float Function
OpStore %_0_x %float_1
%24 = OpLoad %float %_0_x
%23 = OpExtInst %float %1 Length %24
OpStore %_0_x %23
%26 = OpLoad %float %_0_x
%25 = OpExtInst %float %1 Distance %26 %float_2
OpStore %_0_x %25
%29 = OpLoad %float %_0_x
%28 = OpFMul %float %29 %float_2
OpStore %_0_x %28
%31 = OpLoad %float %_0_x
%30 = OpExtInst %float %1 Normalize %31
OpStore %_0_x %30
%33 = OpLoad %float %_0_x
OpStore %x %33
OpStore %_1_x %37
%39 = OpLoad %v2float %_1_x
%38 = OpExtInst %float %1 Length %39
%40 = OpCompositeConstruct %v2float %38 %38
OpStore %_1_x %40
%42 = OpLoad %v2float %_1_x
%41 = OpExtInst %float %1 Distance %42 %45
%46 = OpCompositeConstruct %v2float %41 %41
OpStore %_1_x %46
%48 = OpLoad %v2float %_1_x
%47 = OpDot %float %48 %45
%49 = OpCompositeConstruct %v2float %47 %47
OpStore %_1_x %49
%51 = OpLoad %v2float %_1_x
%50 = OpExtInst %v2float %1 Normalize %51
OpStore %_1_x %50
%53 = OpLoad %v2float %_1_x
OpStore %y %53
%54 = OpAccessChain %_ptr_Uniform_v4float %10 %int_0
%58 = OpLoad %v4float %54
OpReturnValue %58
OpFunctionEnd
