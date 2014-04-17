/** get attributes of a managed file
 *
 * @param e: the export managing the file
 * @param fid: the id of the file
 * @param attrs: attributes to fill.
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
int exports_getattr_nb(export_t *e, fid_t fid, mattr_t *attrs) {
    int status = -1;
    lv2_entry_t *lv2 = 0;
    START_PROFILING(export_getattr);

    if (!(lv2 = export_lookup_fid(e, fid))) {
        severe("export_getattr failed: %s", strerror(errno));
        goto out;
    }
    memcpy(attrs, &lv2->attributes, sizeof (mattr_t));

    status = 0;
out:
    STOP_PROFILING(export_getattr);
    return status;
}
