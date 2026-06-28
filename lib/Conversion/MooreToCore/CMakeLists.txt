add_circt_conversion_library(CIRCTMooreToCore
  LowerClassRandomize.cpp
  MooreToCore.cpp

  DEPENDS
  CIRCTConversionPassIncGen

  LINK_COMPONENTS
  Core

  LINK_LIBS PUBLIC
  CIRCTComb
  CIRCTDebug
  CIRCTHW
  CIRCTLLHD
  CIRCTMoore
  CIRCTSim
  CIRCTVerif
  CIRCTTransforms
  MLIRArithDialect
  MLIRControlFlowDialect
  MLIRControlFlowTransforms
  MLIRFuncDialect
  MLIRSCFDialect
  MLIRSCFToControlFlow
  MLIRSideEffectInterfaces
  MLIRTransforms
)
