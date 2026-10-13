# El núcleo en C++ conecta sus funciones con Nucleo por nombre (JNI): no se renombra ni se quita.
-keep class com.wavapp.mezcla.Nucleo { *; }
-keepclasseswithmembernames,includedescriptorclasses class * { native <methods>; }
