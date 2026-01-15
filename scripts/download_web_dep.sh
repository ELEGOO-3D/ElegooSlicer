#!/bin/bash

set -e

GIT_TOKEN=""
LAN_WEB_URL="https://github.com/ELEGOO-3D/elegoo-fdm-web/releases/download/v2.0.0/lan_service_web.zip"
CLOUD_WEB_URL="https://github.com/ELEGOO-3D/elegoo-fdm-web/releases/download/v2.0.0/cloud_service_web.zip"
LAN_WEB_NAME="lan_service_web"
CLOUD_WEB_NAME="cloud_service_web"

# Check parameter and set env file
ENV_FILE=".env"
if [ "$1" = "test" ]; then
    ENV_FILE=".env.testing"
    echo "Using testing environment"
else
    echo "Using default environment"
fi

echo "Read env from $ENV_FILE"
if [ ! -f "$ENV_FILE" ]; then
    echo "ERROR: $ENV_FILE file not found. Exiting."
    exit 1
fi

# Parse env file, skip empty lines and comments
# Add a newline to ensure the last line is processed
while IFS= read -r line || [ -n "$line" ]; do
    # Skip empty lines
    [ -z "$line" ] && continue
    # Skip comments
    [[ "$line" =~ ^[[:space:]]*# ]] && continue
    
    # Remove inline comments
    line=$(echo "$line" | cut -d'#' -f1)
    
    # Extract key and value using parameter expansion
    if [[ "$line" == *"="* ]]; then
        key="${line%%=*}"
        value="${line#*=}"
        
        # Trim leading/trailing whitespace
        key="${key#"${key%%[![:space:]]*}"}"
        key="${key%"${key##*[![:space:]]}"}"
        value="${value#"${value%%[![:space:]]*}"}"
        value="${value%"${value##*[![:space:]]}"}"
        
        # Export variable
        if [ -n "$key" ]; then
            export "$key=$value"
        fi
    fi
done < "$ENV_FILE"

echo "Download web dependencies"
LAN_WEB_PATH="resources/plugins/elegoolink/web/$LAN_WEB_NAME"
CLOUD_WEB_PATH="resources/plugins/elegoolink/web/$CLOUD_WEB_NAME"

# Check if LAN_WEB_URL is empty
if [ -z "$LAN_WEB_URL" ]; then
    echo "WARNING: LAN_WEB_URL is empty, skipping LAN web download."
else
    echo "Download $LAN_WEB_URL"
    
    curl -L -H "Authorization: Bearer $GIT_TOKEN" -o "$LAN_WEB_NAME.zip" "$LAN_WEB_URL"
    if [ $? -ne 0 ]; then
        echo "ERROR: Download LAN web failed. Exiting."
        exit 1
    fi
    
    echo "Extract $LAN_WEB_NAME.zip"
    
    if [ -d "$LAN_WEB_PATH" ]; then
        # Delete the folder and all its contents
        rm -rf "$LAN_WEB_PATH"
        if [ $? -ne 0 ]; then
            echo "ERROR: Failed to delete $LAN_WEB_PATH. Exiting."
            exit 1
        fi
        echo "Folder $LAN_WEB_PATH and all its contents have been deleted."
    else
        echo "Folder $LAN_WEB_PATH does not exist."
    fi
    
    # Create parent directory if it doesn't exist
    mkdir -p "$(dirname "$LAN_WEB_PATH")"
    
    unzip -q "$LAN_WEB_NAME.zip" -d "$LAN_WEB_PATH"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to extract $LAN_WEB_NAME.zip. Exiting."
        exit 1
    fi
    
    rm "$LAN_WEB_NAME.zip"
    if [ $? -ne 0 ]; then
        echo "WARNING: Failed to delete $LAN_WEB_NAME.zip"
    fi
fi

# Check if CLOUD_WEB_URL is empty
if [ -z "$CLOUD_WEB_URL" ]; then
    echo "WARNING: CLOUD_WEB_URL is empty, skipping CLOUD web download."
else
    echo "Download $CLOUD_WEB_URL"
    
    curl -L -H "Authorization: Bearer $GIT_TOKEN" -o "$CLOUD_WEB_NAME.zip" "$CLOUD_WEB_URL"
    if [ $? -ne 0 ]; then
        echo "ERROR: Download CLOUD web failed. Exiting."
        exit 1
    fi
    
    echo "Extract $CLOUD_WEB_NAME.zip"
    
    if [ -d "$CLOUD_WEB_PATH" ]; then
        # Delete the folder and all its contents
        rm -rf "$CLOUD_WEB_PATH"
        if [ $? -ne 0 ]; then
            echo "ERROR: Failed to delete $CLOUD_WEB_PATH. Exiting."
            exit 1
        fi
        echo "Folder $CLOUD_WEB_PATH and all its contents have been deleted."
    else
        echo "Folder $CLOUD_WEB_PATH does not exist."
    fi
    
    # Create parent directory if it doesn't exist
    mkdir -p "$(dirname "$CLOUD_WEB_PATH")"
    
    unzip -q "$CLOUD_WEB_NAME.zip" -d "$CLOUD_WEB_PATH"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to extract $CLOUD_WEB_NAME.zip. Exiting."
        exit 1
    fi
    
    rm "$CLOUD_WEB_NAME.zip"
    if [ $? -ne 0 ]; then
        echo "WARNING: Failed to delete $CLOUD_WEB_NAME.zip"
    fi
fi

echo "All downloads and extractions completed successfully!"
exit 0
