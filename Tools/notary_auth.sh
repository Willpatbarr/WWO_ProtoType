# Sourced by package_mac.sh and notarize_mac.sh: sets NOTARY_AUTH, the arguments notarytool logs in with.
#
# 1. An App Store Connect API key (preferred: works with the Mac locked, over SSH and from Claude's sessions).
#    Made once at appstoreconnect.apple.com > Users and Access > Integrations > App Store Connect API >
#    Team Keys (access: Developer). Put the downloaded AuthKey_<KEY ID>.p8 in ~/.wwo-notary/ (chmod 600)
#    and write ~/.wwo-notary/key.env with the two ids shown on that page (not secret):
#      KEY_ID=<Key ID>
#      ISSUER=<Issuer ID>
#    The .p8 is the secret: it stays in ~/.wwo-notary, never in git.
# 2. Else the login stored with notarytool store-credentials (profile WWO_NOTARY_PROFILE, default wwo-notary).
#    macOS only hands that out while the Mac is unlocked: with the screen locked notarytool says
#    "No Keychain password item found" though it is stored.
NOTARY_DIR="${WWO_NOTARY_DIR:-$HOME/.wwo-notary}"
NOTARY_AUTH=()
NOTARY_HOW=""
if [ -f "$NOTARY_DIR/key.env" ]; then
  KEY_ID="$(sed -n 's/^KEY_ID=//p' "$NOTARY_DIR/key.env" | tr -d '[:space:]')"
  ISSUER="$(sed -n 's/^ISSUER=//p' "$NOTARY_DIR/key.env" | tr -d '[:space:]')"
  if [ -n "$KEY_ID" ] && [ -n "$ISSUER" ] && [ -f "$NOTARY_DIR/AuthKey_$KEY_ID.p8" ]; then
    NOTARY_AUTH=(--key "$NOTARY_DIR/AuthKey_$KEY_ID.p8" --key-id "$KEY_ID" --issuer "$ISSUER")
    NOTARY_HOW="API key $KEY_ID"
  fi
fi
if [ ${#NOTARY_AUTH[@]} -eq 0 ]; then
  NOTARY_AUTH=(--keychain-profile "${WWO_NOTARY_PROFILE:-wwo-notary}")
  NOTARY_HOW="keychain profile ${WWO_NOTARY_PROFILE:-wwo-notary}"
fi

# notary_ok: 0 when notarytool can log in with NOTARY_AUTH.
notary_ok() { xcrun notarytool history "${NOTARY_AUTH[@]}" >/dev/null 2>&1; }

notary_help() {
  echo "Notarization login ($NOTARY_HOW) can't be used."
  if [ "${NOTARY_HOW#API key}" = "$NOTARY_HOW" ]; then
    echo "No API key in $NOTARY_DIR (see Tools/notary_auth.sh to set one up: it works with the Mac locked)."
    echo "The keychain login only works while the Mac's screen is unlocked."
  else
    echo "Check $NOTARY_DIR/key.env's KEY_ID and ISSUER against App Store Connect, and that the key is not revoked."
  fi
}
