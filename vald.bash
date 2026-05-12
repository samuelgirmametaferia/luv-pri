#!/bin/bash

# Formatting colors
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Starting Verification of LlamaIndex Leaks...${NC}\n"

# --- 1. NEON POSTGRES CHECK ---
echo -e "${BLUE}[1/5] Checking Neon Postgres (neondb_owner)...${NC}"
psql "postgresql://neondb_owner:npg_HEOyGDg5uvI1@ep-muddy-mode-adewajkf-pooler.c-2.us-east-1.aws.neon.tech:5432/neondb?sslmode=require" -c "\dt" > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}[!] SUCCESS: Neon DB is LIVE. Tables:${NC}"
    psql "postgresql://neondb_owner:npg_HEOyGDg5uvI1@ep-muddy-mode-adewajkf-pooler.c-2.us-east-1.aws.neon.tech:5432/neondb?sslmode=require" -c "\dt"
else
    echo -e "${RED}[X] FAILED: Neon DB credentials rejected.${NC}"
fi
echo ""

# --- 2. PRISMA POSTGRES CHECK ---
echo -e "${BLUE}[2/5] Checking Prisma Postgres...${NC}"
psql "postgres://685e6cb94c8b8a5f19089c160a4c455ed1f59f0b4a3c8960a8b4baae7c9d1af9:sk_VanNFOe_9mcvfqrKSEq7Y@db.prisma.io:5432" -c "\dt" > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}[!] SUCCESS: Prisma DB is LIVE. Tables:${NC}"
    psql "postgres://685e6cb94c8b8a5f19089c160a4c455ed1f59f0b4a3c8960a8b4baae7c9d1af9:sk_VanNFOe_9mcvfqrKSEq7Y@db.prisma.io:5432" -c "\dt"
else
    echo -e "${RED}[X] FAILED: Prisma DB credentials rejected.${NC}"
fi
echo ""

# --- 3. GOOGLE GEMINI API CHECK ---
echo -e "${BLUE}[3/5] Checking Google Gemini API...${NC}"
GEMINI_RESP=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
    "https://generativelanguage.googleapis.com/v1beta/models/gemini-pro:generateContent?key=AIzaSyAPy908j7n1P2HlFE1ji3BKONRBps1_5i8" \
    -H 'Content-Type: application/json' \
    -d '{"contents": [{"parts":[{"text": "ping"}]}]}')

if [ "$GEMINI_RESP" == "200" ]; then
    echo -e "${GREEN}[!] SUCCESS: Gemini API Key is ACTIVE (HTTP 200)${NC}"
else
    echo -e "${RED}[X] FAILED: Gemini API Key returned HTTP $GEMINI_RESP${NC}"
fi
echo ""

# --- 4. LANGFUSE API CHECK ---
echo -e "${BLUE}[4/5] Checking Langfuse API...${NC}"
LANGFUSE_AUTH=$(echo -n ":sk-lf-aec2d812-0e49-4f45-a1db-7f6fe6d8a108" | base64)
LANGFUSE_RESP=$(curl -s -o /dev/null -w "%{http_code}" -X GET "https://cloud.langfuse.com/api/public/health" \
    -H "Authorization: Basic $LANGFUSE_AUTH")

if [ "$LANGFUSE_RESP" == "200" ]; then
    echo -e "${GREEN}[!] SUCCESS: Langfuse API Key is ACTIVE (HTTP 200)${NC}"
else
    echo -e "${RED}[X] FAILED: Langfuse API Key returned HTTP $LANGFUSE_RESP${NC}"
fi
echo ""

# --- 5. ELEVENLABS API CHECK ---
echo -e "${BLUE}[5/5] Checking ElevenLabs API...${NC}"
ELEVEN_RESP=$(curl -s -o /dev/null -w "%{http_code}" -X GET "https://api.elevenlabs.io/v1/user" \
    -H "xi-api-key: fa41e08367fb9d798399e9e9431d982c")

if [ "$ELEVEN_RESP" == "200" ]; then
    echo -e "${GREEN}[!] SUCCESS: ElevenLabs API Key is ACTIVE (HTTP 200)${NC}"
else
    echo -e "${RED}[X] FAILED: ElevenLabs API Key returned HTTP $ELEVEN_RESP${NC}"
fi

echo -e "\n${BLUE}Verification Complete.${NC}"
