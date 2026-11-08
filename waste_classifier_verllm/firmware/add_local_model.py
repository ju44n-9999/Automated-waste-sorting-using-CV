Import("env")

env.BuildSources(
    "$BUILD_DIR/local_model",
    env.subst("$PROJECT_DIR/../../firmware/arduino/WasteClassifier"),
    "+<model_data.cpp>",
)
