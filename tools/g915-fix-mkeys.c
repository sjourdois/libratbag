/*
 * g915-fix-mkeys - restaure la bascule native des touches M sur un profil.
 *
 * Sur le G915, chaque bouton d'un profil embarque occupe 4 octets. L'action
 * native "aller au profil N" s'ecrit `90 0d ff NN`, NN etant le numero de
 * profil en base 1. libratbag ne sait pas representer cette action : il la lit
 * `unknown` et ne peut donc pas la reecrire (cf. libratbag#1104, sans reponse).
 * Une fois ecrasee, elle n'est recuperable qu'en ecrivant les octets a la main.
 *
 * Les trois touches M sont aux offsets 0x34, 0x38, 0x3c du secteur du profil.
 *
 * Usage : g915-fix-mkeys <secteur> /dev/hidrawN
 *         (secteur 1 = profil 0, secteur 2 = profil 1, etc.)
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hidpp20.h>
#include <libratbag-util.h>

#define MKEY_OFFSET 0x34
#define MKEY_STRIDE 0x04
#define MKEY_COUNT  3

static void
usage(void)
{
	printf("Usage: %s <secteur> /dev/hidraw0\n", program_invocation_short_name);
	printf("  secteur 1 = profil 0, secteur 2 = profil 1, ...\n");
}

int
main(int argc, char **argv)
{
	_cleanup_close_ int fd = -1;
	_cleanup_free_ uint8_t *data = NULL;
	const char *path;
	uint16_t sector;
	struct hidpp20_device *dev = NULL;
	struct hidpp_device base;
	struct hidpp20_onboard_profiles_info info = { 0 };
	int rc, i;

	if (argc != 3) {
		usage();
		return 1;
	}

	sector = atoi(argv[1]);
	path = argv[2];

	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Ouverture de '%s' impossible : %s\n", path, strerror(errno));
		return 3;
	}

	hidpp_device_init(&base, fd);
	dev = hidpp20_device_new(&base, 0xff, NULL, 0);
	if (!dev) {
		fprintf(stderr, "%s n'est pas un peripherique HID++ 2.0\n", path);
		return 3;
	}

	hidpp20_onboard_profiles_get_profiles_desc(dev, &info);

	data = zalloc(info.sector_size);

	rc = hidpp20_onboard_profiles_read_sector(dev, sector, info.sector_size, data);
	if (rc < 0) {
		fprintf(stderr, "Lecture du secteur %u impossible : %d\n", sector, rc);
		goto out;
	}

	printf("Secteur %u, avant :\n", sector);
	for (i = 0; i < MKEY_COUNT; i++) {
		uint8_t *p = data + MKEY_OFFSET + i * MKEY_STRIDE;
		printf("  M%d (0x%02x) : %02x %02x %02x %02x\n",
		       i + 1, MKEY_OFFSET + i * MKEY_STRIDE, p[0], p[1], p[2], p[3]);
	}

	/* action native : aller au profil (i+1), numerotation en base 1 */
	for (i = 0; i < MKEY_COUNT; i++) {
		uint8_t *p = data + MKEY_OFFSET + i * MKEY_STRIDE;
		p[0] = 0x90;
		p[1] = 0x0d;
		p[2] = 0xff;
		p[3] = i + 1;
	}

	printf("Secteur %u, apres :\n", sector);
	for (i = 0; i < MKEY_COUNT; i++) {
		uint8_t *p = data + MKEY_OFFSET + i * MKEY_STRIDE;
		printf("  M%d (0x%02x) : %02x %02x %02x %02x\n",
		       i + 1, MKEY_OFFSET + i * MKEY_STRIDE, p[0], p[1], p[2], p[3]);
	}

	rc = hidpp20_onboard_profiles_write_sector(dev, sector, info.sector_size, data, true);
	if (rc < 0) {
		fprintf(stderr, "Ecriture du secteur %u impossible : %d\n", sector, rc);
		goto out;
	}

	printf("Ecrit.\n");
	rc = 0;

out:
	hidpp20_device_destroy(dev);
	return rc < 0 ? 1 : 0;
}
