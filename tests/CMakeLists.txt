
find_package(CppUnit REQUIRED)
if(NOT MSVC)
	check_link_library(CppUnit CppUnit_LIBRARIES)
endif()

set(arxtest_TESTCASES
	
	tests/TestMain.cpp
	
	tests/graphics/ColorTest.h
	tests/graphics/ColorTest.cpp
	
	tests/core/GameEventTest.h
	tests/core/GameEventTest.cpp
	
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
	
	tests/math/AssertionTraits.h
	tests/math/LegacyMath.h
	tests/math/LegacyMathTest.cpp
	
	tests/util/StringTest.h
	tests/util/StringTest.cpp

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

	tests/vr/VRGameInputTest.h
	tests/vr/VRGameInputTest.cpp

	tests/vr/VRTurnTest.h
	tests/vr/VRTurnTest.cpp

	tests/vr/EyeImageTest.h
	tests/vr/EyeImageTest.cpp
	tests/vr/SimRigTest.h
	tests/vr/SimRigTest.cpp
	tests/vr/UIPanelTest.h
	tests/vr/UIPanelTest.cpp
	tests/vr/InterfaceSizeTest.h
	tests/vr/InterfaceSizeTest.cpp
	tests/vr/LocomotionTest.h
	tests/vr/LocomotionTest.cpp

	tests/vr/ArmIKTest.h
	tests/vr/ArmIKTest.cpp
	tests/vr/WeaponGripTest.h
	tests/vr/WeaponGripTest.cpp


)

# CppUnit requires RTTI and does not work with libstdc++'s debug mode
add_source_flags("${arxtest_TESTCASES}" "${enable_rtti} ${disable_libstdcxx_debug}")

set(arxtest_SOURCES
	
	${BASE_SOURCES}
	${MATH_SOURCES}
	
	src/game/Camera.cpp
	src/graphics/Math.cpp
	src/graphics/Renderer.cpp
	src/io/IniSection.cpp
	src/io/log/FileLogger.cpp
	src/io/resource/ResourcePath.cpp
	src/vr/BodyYaw.cpp
	src/vr/EyeProjection.cpp
	src/vr/FramePacing.cpp
	src/vr/VRBindings.cpp
	src/vr/HeadLean.cpp
	src/vr/HeadPose.cpp
	src/vr/HeadReferenceGate.cpp
	src/vr/PhysicalCrouch.cpp
	src/vr/VRControls.cpp
	src/vr/VRGameInput.cpp
	src/vr/VRTurn.cpp
	src/vr/EyeImage.cpp
	src/vr/SimRig.cpp
	src/vr/UIPanel.cpp
	src/vr/InterfaceSize.cpp
	src/vr/ArmIK.cpp
	src/vr/WeaponGrip.cpp
	src/vr/Locomotion.cpp


	${arxtest_TESTCASES}
	
)

set(arxtest_INCLUDES
	${CppUnit_INCLUDE_DIR}
)

set(arxtest_LIBRARIES ${BASE_LIBRARIES} ${CppUnit_LIBRARIES})

add_executable_shared(arxtest "${arxtest_SOURCES}" "${arxtest_LIBRARIES}")
add_binary_includes(arxtest ${arxtest_INCLUDES})
set_binary_installdir(arxtest "")
