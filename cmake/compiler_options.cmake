function(set_compiler_options target)
    target_compile_options(${target} PRIVATE
        /W4
        /w14640
        /w14242
        /w14254
        /w14263
        /w14265
        /w14287
        /we4289
        /w14296
        /w14311
        /w14545
        /w14546
        /w14547
        /w14549
        /w14555
        /w14619
        /w14640
        /w14826
        /w14905
        /w14906
        /w14928
        /wd4710
        /wd4711
        /external:anglebrackets
        /external:W0
        /analyze:external-
        /analyze
        /sdl
        /MP
        /guard:cf
        /utf-8
        /Zc:__cplusplus
        /Zc:inline
        /Zc:preprocessor
        /Zc:rvalueCast
        /Zc:throwingNew
        /GF
        /Gy
        /GR-
        /permissive-
        $<$<CONFIG:Debug>: /fsanitize=address>
        $<$<CONFIG:Release>: /O2 /Ob2 /GL /GT /Oi>
    )

    target_link_options(${target} PRIVATE
        /CGTHREADS:8
        /CETCOMPAT
        /DYNAMICBASE
        /HIGHENTROPYVA
        /INCREMENTAL:NO
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )
endfunction()