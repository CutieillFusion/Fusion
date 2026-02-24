# FusionLLVMComponents.cmake
# Single source of truth for LLVM components needed for static linking (IR + OrcJIT + X86 codegen).
# Include after LLVM-Config.cmake; then call llvm_map_components_to_libnames(FUSION_LLVM_LIBS ${FUSION_LLVM_COMPONENTS}).
# Pre-built LLVM does not set LLVM_LINK_COMPONENTS, so we must list all transitive deps explicitly.
#
# Direct: AsmPrinter, GlobalISel, Passes, ProfileData, OrcJIT, X86CodeGen, JITLink, AsmPrinter, Core, etc.
# Transitive (from INTERFACE_LINK_LIBRARIES):
#   TargetParser: Triple (Core, AsmPrinter, Object, ...)
#   Option: OptTable, InputArgList (JITLink COFF parser)
#   Remarks: RemarkStreamer (Core)
#   BitWriter: BitcodeWriter (OrcJIT cloneToNewContext)
#   DebugInfoCodeView: codeview:: (AsmPrinter CodeViewDebug)
#   DebugInfoDWARF: DWARFContext, DWARFDie (AsmPrinter, OrcJIT, ProfileData)
#   CFGuard: createCFGuardDispatchPass (X86CodeGen)
#   ObjCARCOpts: initializeObjCARCContractLegacyPassPass (CodeGen)
#   OrcTargetProcess: llvm_orc_registerEHFrameSectionWrapper (OrcJIT)
#   DWARFLinker: AsmPrinter DwarfDebug
#   WindowsDriver: OrcJIT
#   IRPrinter, Instrumentation, ScalarOpts, ipo: CodeGen/X86CodeGen
#   InstCombine, AggressiveInstCombine: Passes
#   MIRParser: Passes/CodeGen
#   Extensions, FrontendOpenMP, etc.: Passes
#   CodeGenTypes: LLT (CodeGen/GlobalISel)
#   IRReader: getLazyIRModule (Object OffloadBinary)
#   TextAPI: MachO TextAPI (Object TapiFile, TapiUniversal)
set(FUSION_LLVM_COMPONENTS
  OrcJIT ExecutionEngine Core Support Demangle
  MC MCParser MCDisassembler Object RuntimeDyld JITLink OrcShared OrcTargetProcess
  Target CodeGen CodeGenTypes SelectionDAG AsmPrinter GlobalISel X86CodeGen X86AsmParser X86Desc X86Info
  Analysis TransformUtils Passes ProfileData
  BinaryFormat BitReader BitWriter BitstreamReader
  IRReader
  AsmParser
  TextAPI
  TargetParser Option Remarks
  DebugInfoCodeView DebugInfoDWARF DWARFLinker
  CFGuard ObjCARCOpts
  IRPrinter Instrumentation ScalarOpts ipo InstCombine AggressiveInstCombine
  MIRParser Extensions FrontendOpenMP FrontendOffloading
  WindowsDriver
)
