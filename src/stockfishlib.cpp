/*
     Copyright (C) 2021	Alexander Bootman, alexbootman@gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

//#include </Library/Java/JavaVirtualMachines/jdk-9.0.1.jdk/Contents/Home/include/jni.h>
//#include </Library/Java/JavaVirtualMachines/jdk-9.0.1.jdk/Contents/Home/include/darwin/jni_md.h>
#include <jni.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <sstream>

#include "uci.h"

#ifndef _Included_com_ab_pgn_stockfish_Stockfish
#define _Included_com_ab_pgn_stockfish_Stockfish
#ifdef __cplusplus
extern "C" {
#endif

/*
* Class:     com_ab_pgn_stockfish_Stockfish
* Method:    _init
* Signature: ()V
*/
JNIEXPORT void JNICALL
Java_com_ab_pgn_stockfish_Stockfish__1init
    (JNIEnv *, jobject) {
    sf_init();
}

/*
 * Class:     com_ab_pgn_stockfish_Stockfish
 * Method:    _write
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL
Java_com_ab_pgn_stockfish_Stockfish__1write
    (JNIEnv *env, jobject, jstring _command) {
	const char *cmd = env->GetStringUTFChars(_command, NULL);
    Stockfish::UCI::execute(cmd);
}

/*
 * Class:     com_ab_pgn_stockfish_Stockfish
 * Method:    _read
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL
Java_com_ab_pgn_stockfish_Stockfish__1read
    (JNIEnv *env, jobject) {
printf("outstream.read() blocking\n");
    std::string from_uci;
    int len = outstream.read(from_uci);
printf("outstream.read() unblocked\n");
    jstring res;
    if (len >= 0) {
        res = env->NewStringUTF(from_uci.c_str());
    }
    return res;
}

/*
 * Class:     com_ab_pgn_stockfish_Stockfish
 * Method:    _read_err
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL
Java_com_ab_pgn_stockfish_Stockfish__1read_1err
        (JNIEnv *env, jobject) {
//    std::string from_uci = errstream.read();
//    jstring res = env->NewStringUTF(from_uci.c_str());
//    return res;
    std::string from_uci;
    int len = errstream.read(from_uci);
    printf("errstream.read() unblocked\n");
    jstring res;
    if (len >= 0) {
        res = env->NewStringUTF(from_uci.c_str());
    }
    return res;
}

#ifdef __cplusplus
}
#endif
#endif

