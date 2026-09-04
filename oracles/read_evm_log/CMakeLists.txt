# Python service - nothing to build; install the script as an executable named like the others.
install(PROGRAMS read_evm_log_service.py
        DESTINATION bin
        RENAME read_evm_log_service)
