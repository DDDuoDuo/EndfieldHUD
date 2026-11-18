# App shortcuts

Use + Add App to choose an installed application. Give it a name and choose an icon, then launch it from the HUD's right-side buttons.

Shortcuts can be renamed or removed from their editing controls. Removing a shortcut does not uninstall the app. The right-side list scrolls when more buttons are added.

## Windows shortcut groups

Open **Groups** in the right-side navigation, then **New group**. Each group has an editable name and a square grid of shortcuts. Add an application through the chooser, or drag `.exe` / `.lnk` shortcuts into the group. Original icons and launch targets use the existing Windows shortcut service. Desktop files are not moved or deleted.

Outside edit mode, click a group to open it and double-click a shortcut to launch it. In either the groups overview or a group, choose **Edit** to drag cards into a new order. Click cards to toggle multiple selections, click empty space to clear selection, and choose **Delete** to confirm removal. Deleting a group also removes its shortcut references. Choose **Done** to resume navigation and launching.

Groups, membership and ordering are saved with the existing Windows shortcut data. The same application may appear in different groups, while duplicates within the same group are rejected. Existing ungrouped shortcuts remain in **Add App** and the right-side list.

This feature is Windows-only. The existing version-1 shortcut JSON gains optional `groups` and per-item `groupID` fields. Old ungrouped files remain readable; older clients do not display groups and should not be used to edit grouped data. Batch removals are queued individual transactions: an error on one removal does not roll back earlier successful removals.
