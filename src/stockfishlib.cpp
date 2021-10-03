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
* Method:    _quit
* Signature: ()V
*/
JNIEXPORT void JNICALL
Java_com_ab_pgn_stockfish_Stockfish__1quit
    (JNIEnv *, jobject) {
    unblock_readers();
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
    if (strcmp(cmd, "quit") == 0) {
        unblock_readers();
    }
}

jstring _read(JNIEnv *env, Outstream& os) {
    std::string from_uci;
    int len = os.read(from_uci);
    jstring res = NULL;
    if (len >= 0) {
        res = env->NewStringUTF(from_uci.c_str());
    }
    return res;
}

/*
 * Class:     com_ab_pgn_stockfish_Stockfish
 * Method:    _read
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL
Java_com_ab_pgn_stockfish_Stockfish__1read
    (JNIEnv *env, jobject) {
     return _read(env, outstream);
}

/*
 * Class:     com_ab_pgn_stockfish_Stockfish
 * Method:    _read_err
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL
Java_com_ab_pgn_stockfish_Stockfish__1read_1err
        (JNIEnv *env, jobject) {
    return _read(env, errstream);
}

#ifdef __cplusplus
}
#endif
#endif

