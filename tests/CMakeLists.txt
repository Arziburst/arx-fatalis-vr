
find_package(CppUnit REQUIRED)
if(NOT MSVC)
	check_link_library(CppUnit CppUnit_LIBRARIES)
endif()

set(arxtest_TESTCASES
	
	tests/TestMain.cpp
	
	tests/graphics/ColorTest.h
	tests/graphics/ColorTest.cpp
	tests/graphics/PixelStoreTest.h
	tests/graphics/PixelStoreTest.cpp

	tests/core/ChannelWriterTest.h
	tests/core/ChannelWriterTest.cpp
	tests/core/CommandInboxTest.h
	tests/core/CommandInboxTest.cpp
	tests/core/GameEventTest.h
	tests/core/GameEventTest.cpp
	tests/core/FileReplaceTest.h
	tests/core/FileReplaceTest.cpp
	tests/core/SaveSlotsTest.h
	tests/core/SaveSlotsTest.cpp

	tests/game/JumpPhysicsTest.h
	tests/game/JumpPhysicsTest.cpp
	
	tests/io/fs/FilePathTest.h
	tests/io/fs/FilePathTest.cpp
	
	tests/io/fs/FilesystemTest.h
	tests/io/fs/FilesystemTest.cpp

	tests/io/fs/SteamLibraryTest.h
	tests/io/fs/SteamLibraryTest.cpp

	tests/io/log/FileLoggerTest.h
	tests/io/log/FileLoggerTest.cpp

	tests/io/resource/ResourcePathTest.h
	tests/io/resource/ResourcePathTest.cpp
	
	tests/io/IniTest.h
	tests/io/IniTest.cpp
	
	tests/platform/CrashGateTest.h
	tests/platform/CrashGateTest.cpp
	tests/platform/CrashNoticeTest.h
	tests/platform/CrashNoticeTest.cpp
	tests/platform/CrashNoticeKindTest.h
	tests/platform/CrashNoticeKindTest.cpp
	tests/platform/DialogPolicyTest.h
	tests/platform/DialogPolicyTest.cpp
	tests/platform/ExitReportTest.h
	tests/platform/ExitReportTest.cpp
	tests/platform/FlightRecorderTest.h
	tests/platform/FlightRecorderTest.cpp
	tests/platform/HangWatchdogTest.h
	tests/platform/HangWatchdogTest.cpp
	tests/platform/ProcessResourcesTest.h
	tests/platform/ProcessResourcesTest.cpp

	tests/math/AssertionTraits.h
	tests/math/LegacyMath.h
	tests/math/LegacyMathTest.cpp
	tests/math/RandomTest.h
	tests/math/RandomTest.cpp
	
	tests/util/StringTest.h
	tests/util/StringTest.cpp

	tests/gui/DevVariantsTest.h
	tests/gui/DevVariantsTest.cpp

	tests/vr/BodyYawTest.h
	tests/vr/BodyYawTest.cpp

	tests/vr/HeadLeanTest.h
	tests/vr/HeadLeanTest.cpp

	tests/vr/EyeProjectionTest.h
	tests/vr/EyeProjectionTest.cpp

	tests/vr/FramePacingTest.h
	tests/vr/FramePacingTest.cpp
	tests/vr/MirrorGeometryTest.h
	tests/vr/MirrorGeometryTest.cpp

	tests/vr/VRBindingsTest.h
	tests/vr/VRBindingsTest.cpp

	tests/vr/HeadPoseTest.h
	tests/vr/HeadPoseTest.cpp
	tests/vr/HeadReferenceGateTest.h
	tests/vr/HeadReferenceGateTest.cpp
	tests/vr/PhysicalCrouchTest.h
	tests/vr/PhysicalCrouchTest.cpp

	tests/vr/VRControlsTest.h
	tests/vr/VRControlsTest.cpp
	tests/vr/BindingCaptureTest.h
	tests/vr/BindingCaptureTest.cpp
	tests/vr/MenuReachTest.h
	tests/vr/MenuReachTest.cpp
	tests/vr/MenuRebindTest.h
	tests/vr/MenuRebindTest.cpp
	tests/vr/MarkMomentTest.h
	tests/vr/MarkMomentTest.cpp
	tests/vr/VoiceNoteTest.h
	tests/vr/VoiceNoteTest.cpp
	tests/vr/MarkHoldTest.h
	tests/vr/MarkHoldTest.cpp
	tests/vr/SnapshotImageTest.h
	tests/vr/SnapshotImageTest.cpp

	tests/vr/VRGameInputTest.h
	tests/vr/VRGameInputTest.cpp

	tests/vr/VRTurnTest.h
	tests/vr/VRTurnTest.cpp

	tests/vr/VRSettingsRowsTest.h
	tests/vr/VRSettingsRowsTest.cpp

	tests/vr/EyeImageTest.h
	tests/vr/EyeImageTest.cpp
	tests/vr/SimRigTest.h
	tests/vr/SimRigTest.cpp
	tests/vr/ReplayClipTest.h
	tests/vr/ReplayClipTest.cpp
	tests/vr/ReplayRingTest.h
	tests/vr/ReplayRingTest.cpp
	tests/vr/RecordGateTest.h
	tests/vr/RecordGateTest.cpp
	tests/vr/UIPanelTest.h
	tests/vr/UIPanelTest.cpp
	tests/vr/MenuPointerTest.h
	tests/vr/MenuPointerTest.cpp
	tests/vr/InterfaceSizeTest.h
	tests/vr/InterfaceSizeTest.cpp
	tests/vr/LocomotionTest.h
	tests/vr/LocomotionTest.cpp
	tests/vr/MotionProbeTest.h
	tests/vr/MotionProbeTest.cpp

	tests/vr/ArmIKTest.h
	tests/vr/ArmIKTest.cpp
	tests/vr/ArmRollTest.h
	tests/vr/ArmRollTest.cpp
	tests/vr/ArxArmFixTest.h
	tests/vr/ArxArmFixTest.cpp
	tests/vr/WristVitalsTest.h
	tests/vr/WristVitalsTest.cpp
	tests/vr/HudFrameTest.h
	tests/vr/HudFrameTest.cpp
	tests/vr/WeaponGripTest.h
	tests/vr/WeaponGripTest.cpp
	tests/vr/WeaponInGloveTest.h
	tests/vr/WeaponInGloveTest.cpp
	tests/vr/WeaponHiltFrameTest.h
	tests/vr/WeaponHiltFrameTest.cpp
	tests/vr/MeleeSwingTest.h
	tests/vr/MeleeSwingTest.cpp

	tests/vr/HandReticleTest.h
	tests/vr/HandReticleTest.cpp

	tests/vr/RuneTrailTest.h
	tests/vr/RuneTrailTest.cpp

	tests/vr/FingerCurlTest.h
	tests/vr/FingerCurlTest.cpp

	tests/vr/ArmMeshTest.h
	tests/vr/ArmMeshTest.cpp

	tests/vr/HandRigTest.h
	tests/vr/HandRigTest.cpp

	tests/vr/CustomFingersTest.h
	tests/vr/CustomFingersTest.cpp

	tests/vr/HandConformingTest.h
	tests/vr/HandConformingTest.cpp

	tests/vr/HandShapeTest.h
	tests/vr/HandShapeTest.cpp

	tests/vr/ArmFitTest.h
	tests/vr/ArmFitTest.cpp

	tests/vr/BodyViewTest.h
	tests/vr/BodyViewTest.cpp
	tests/vr/BodyPushTest.h
	tests/vr/BodyPushTest.cpp

	tests/vr/RadialMenuTest.h
	tests/vr/RadialMenuTest.cpp

	tests/vr/PerfStatsTest.h
	tests/vr/PerfStatsTest.cpp

	tests/vr/PerfWriterTest.h
	tests/vr/PerfWriterTest.cpp

	tests/vr/SceneViewTest.h
	tests/vr/SceneViewTest.cpp
	tests/vr/SceneSubtitlesTest.h
	tests/vr/SceneSubtitlesTest.cpp


)

# CppUnit requires RTTI and does not work with libstdc++'s debug mode
add_source_flags("${arxtest_TESTCASES}" "${enable_rtti} ${disable_libstdcxx_debug}")

set(arxtest_SOURCES
	
	${BASE_SOURCES}
	${MATH_SOURCES}
	
	src/core/ChannelWriter.cpp
	src/core/CommandInbox.cpp
	src/game/Camera.cpp
	src/graphics/Math.cpp
	src/graphics/Renderer.cpp
	src/gui/DevVariants.cpp
	src/io/IniSection.cpp
	src/io/log/FileLogger.cpp
	src/io/resource/ResourcePath.cpp
	src/platform/CrashGate.cpp
	src/platform/CrashNotice.cpp
	src/platform/ExitReport.cpp
	src/platform/FlightRecorder.cpp
	src/platform/HangWatchdog.cpp
	src/platform/ProcessResources.cpp
	src/vr/BodyYaw.cpp
	src/vr/EyeProjection.cpp
	src/vr/FramePacing.cpp
	src/vr/PerfStats.cpp
	src/vr/PerfWriter.cpp
	src/vr/ReplayClip.cpp
	src/vr/VRBindings.cpp
	src/vr/HeadLean.cpp
	src/vr/HeadPose.cpp
	src/vr/HeadReferenceGate.cpp
	src/vr/PhysicalCrouch.cpp
	src/vr/VRControls.cpp
	src/vr/BindingCapture.cpp
	src/vr/VRGameInput.cpp
	src/vr/VoiceNote.cpp
	src/vr/VRTurn.cpp
	src/vr/VRSettingsRows.cpp
	src/vr/EyeImage.cpp
	src/vr/SimRig.cpp
	src/vr/UIPanel.cpp
	src/vr/MarkMoment.cpp
	src/vr/SnapshotImage.cpp
	src/graphics/image/stb_image.cpp
	src/vr/MenuPointer.cpp
	src/vr/InterfaceSize.cpp
	src/vr/ArmIK.cpp
	src/vr/ArmRoll.cpp
	src/vr/ArxArmFix.cpp
	src/vr/WristVitals.cpp
	src/vr/HandReticle.cpp
	src/vr/WeaponGrip.cpp
	src/vr/Locomotion.cpp
	src/vr/FingerCurl.cpp
	src/vr/ArmMesh.cpp
	src/vr/HandRig.cpp
	src/vr/ArmFit.cpp
	src/vr/BodyView.cpp
	src/vr/RuneTrail.cpp
	src/vr/MeleeSwing.cpp
	src/vr/MotionProbe.cpp
	src/vr/SceneView.cpp
	src/vr/RadialMenu.cpp
	src/vr/HudFrame.cpp


	${arxtest_TESTCASES}
	
)

set(arxtest_INCLUDES
	${CppUnit_INCLUDE_DIR}
)

set(arxtest_LIBRARIES ${BASE_LIBRARIES} ${CppUnit_LIBRARIES})

add_executable_shared(arxtest "${arxtest_SOURCES}" "${arxtest_LIBRARIES}")
add_binary_includes(arxtest ${arxtest_INCLUDES})
set_binary_installdir(arxtest "")
