// Keys of time stamping authorities (signers or intermediate CAs) known to be compromised: SHA-256 of the
// SubjectPublicKeyInfo, hex. Revocation lists do not help here: a CA drops a certificate from them once it expires, and
// the check works offline. A token signed with such a key, or under such an intermediate, is not trusted whatever its
// time (a stolen key stamps any time within its certificate). Add with the authority's announcement in a comment.
