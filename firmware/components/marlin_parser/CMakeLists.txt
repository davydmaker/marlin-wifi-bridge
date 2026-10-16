idf_component_register(
    SRCS "marlin_parser.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log telemetry_store
)
