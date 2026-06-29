
orcaslicer_add_cmake_project(PahoMqttCpp
  GIT_REPOSITORY      https://github.com/eclipse-paho/paho.mqtt.cpp.git
  GIT_TAG             v1.5.3

    DEPENDS ${OPENSSL_PKG}
    CMAKE_ARGS 
        -DPAHO_BUILD_STATIC:BOOL=ON
        -DPAHO_BUILD_DOCUMENTATION:BOOL=OFF
        -DPAHO_BUILD_SAMPLES:BOOL=OFF
        -DPAHO_BUILD_TESTS:BOOL=OFF
        -DPAHO_WITH_SSL:BOOL=ON
        -DPAHO_WITH_MQTT_C:BOOL=ON
)