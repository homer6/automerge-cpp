use automerge::{transaction::Transactable, ActorId, AutoCommit, ObjType, ROOT};
use std::fs;
use std::path::Path;

fn actor(bytes: u8) -> ActorId {
    ActorId::from([bytes; 16])
}

fn write_fixture(name: &str, document: &mut AutoCommit) {
    let fixture_directory = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
    let saved_bytes = document.save_after(&[]);
    let hashes = document
        .get_changes(&[])
        .iter()
        .map(|change| change.hash().to_string())
        .collect::<Vec<_>>()
        .join("\n");

    fs::write(fixture_directory.join(format!("{name}.am")), saved_bytes).unwrap();
    fs::write(
        fixture_directory.join(format!("{name}.hashes")),
        format!("{hashes}\n"),
    )
    .unwrap();
    fs::write(
        fixture_directory.join(format!("{name}.document.am")),
        document.save(),
    )
    .unwrap();
}

fn map_puts() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x11));
    document.put(ROOT, "title", "Automerge").unwrap();
    document.put(ROOT, "version", 1).unwrap();
    document.commit();
    document
}

fn list_inserts() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x22));
    let list = document.put_object(ROOT, "items", ObjType::List).unwrap();
    document.insert(&list, 0, "first").unwrap();
    document.insert(&list, 1, "second").unwrap();
    document.commit();
    document
}

fn text_splice() -> AutoCommit {
    let mut document = AutoCommit::new().with_actor(actor(0x33));
    let text = document.put_object(ROOT, "body", ObjType::Text).unwrap();
    document.splice_text(&text, 0, 0, "hello").unwrap();
    document.splice_text(&text, 1, 3, "i").unwrap();
    document.commit();
    document
}

fn two_actor_merge() -> AutoCommit {
    let mut first = AutoCommit::new().with_actor(actor(0x44));
    first.put(ROOT, "shared", "base").unwrap();
    first.commit();

    let mut second = first.fork().with_actor(actor(0x55));
    first.put(ROOT, "left", true).unwrap();
    first.commit();
    second.put(ROOT, "right", true).unwrap();
    second.commit();
    first.merge(&mut second).unwrap();
    first
}

fn write_incremental_fixture() {
    let fixture_directory = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
    let mut document = AutoCommit::new().with_actor(actor(0x66));

    document.put(ROOT, "stage", "b".repeat(400)).unwrap();
    document.commit();
    fs::write(
        fixture_directory.join("incremental_base.am"),
        document.save(),
    )
    .unwrap();

    document.put(ROOT, "first", 1).unwrap();
    document.commit();
    fs::write(
        fixture_directory.join("incremental_first.am"),
        document.save_incremental(),
    )
    .unwrap();

    document.put(ROOT, "second", 2).unwrap();
    document.commit();
    fs::write(
        fixture_directory.join("incremental_second.am"),
        document.save_incremental(),
    )
    .unwrap();
}

fn main() {
    write_fixture("map_puts", &mut map_puts());
    write_fixture("list_inserts", &mut list_inserts());
    write_fixture("text_splice", &mut text_splice());
    write_fixture("two_actor_merge", &mut two_actor_merge());
    write_incremental_fixture();
}
