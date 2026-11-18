# Protocol Buffers C++ runtime integration for qmake targets.
#
# Set PROTOBUF_ROOT to a protobuf installation prefix when it is outside the
# compiler's default search paths. Otherwise qmake links the system libprotobuf.
PROTOBUF_ROOT_ENV = $$(PROTOBUF_ROOT)
isEmpty(PROTOBUF_ROOT): PROTOBUF_ROOT = $$PROTOBUF_ROOT_ENV

!isEmpty(PROTOBUF_ROOT) {
    INCLUDEPATH += $$PROTOBUF_ROOT/include
    DEPENDPATH += $$PROTOBUF_ROOT/include
    LIBS += -L$$PROTOBUF_ROOT/lib -lprotobuf
} else {
    unix: LIBS += -lprotobuf
    win32: error("Protocol Buffers runtime not found. Set PROTOBUF_ROOT to its install directory.")
}

# Generated C++ is checked in; protoc is only needed when regenerating schema.
PROTOC = $$[QT_HOST_BINS]/protoc
!exists($$PROTOC): PROTOC = protoc

OTHER_FILES += $$PWD/README.md
