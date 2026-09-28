# The native engine calls into these by name (JNI method registration and ThrowNew).
-keep class org.opentax.app.engine.Native { *; }
-keep class org.opentax.app.engine.OpenTaxException { <init>(java.lang.String); }
-keep class org.opentax.app.engine.PasswordRequiredException { <init>(java.lang.String); }
-keep class org.opentax.app.engine.WrongPasswordException { <init>(java.lang.String); }
