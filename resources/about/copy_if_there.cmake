# Copies FROM to TO when FROM is there (the third-party notices are not in every checkout).
if(EXISTS "${FROM}")
    configure_file("${FROM}" "${TO}" COPYONLY)
endif()
