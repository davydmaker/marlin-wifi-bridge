idf_component_register(
    SRCS "cmd_dispatcher.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES log marlin_comm
)
