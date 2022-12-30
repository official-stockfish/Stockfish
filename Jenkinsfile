pipeline {
    agent any

    stages {
        stage('Hello') {
            steps {
         	sh '''
            	#!/bin/sh
		cd src
    		make help
    		make net
    		make build ARCH=x86-64-modern
         	'''
            }
        }
    }
}


