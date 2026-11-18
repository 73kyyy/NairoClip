# Android Local Data Backup Privacy Fix

## Policy

NairoClip's project drafts, recovery data, and locally installed AI model files are private local data. They must not be silently copied to cloud backup or device-transfer backup.

## Changes

- Set `android:allowBackup="false"` in the application manifest.
- Added `android:fullBackupContent="@xml/backup_rules"` with exclusions for root, files, databases, shared preferences, and external app storage for legacy backup configuration.
- Added `android:dataExtractionRules="@xml/data_extraction_rules"` with exclusions for the same storage domains in both cloud backup and device-to-device transfer rules.

The rules are defense in depth. OEM-specific device migration behavior must still be verified on supported Android versions; no claim is made that manifest configuration alone proves every vendor transfer path is disabled.

## Verification

- Manifest and both XML resource files were committed on `work/v3.3.3-full-remediation`.
- Static readback and Android resource/manifest merge validation are pending.
- No Android build or device backup/restore test has been run.
