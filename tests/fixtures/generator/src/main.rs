use automerge::{
    sync::{Capability, Message, MessageVersion, State, SyncDoc},
    transaction::Transactable,
    ActorId, AutoCommit, ObjType, ROOT,
};
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

fn write_sync_document(path: &Path, document: &mut AutoCommit) {
    fs::write(path, document.save_after(&[])).unwrap();
}

fn encode_v1(message: Message) -> Vec<u8> {
    let mut message = message;
    message.version = MessageVersion::V1;
    message.supported_capabilities = Some(vec![Capability::MessageV1]);
    message.encode()
}

fn sync_exchange() {
    let fixture_directory = Path::new(env!("CARGO_MANIFEST_DIR")).parent().unwrap();
    let exchange_directory = fixture_directory.join("sync_exchange");
    if exchange_directory.exists() {
        fs::remove_dir_all(&exchange_directory).unwrap();
    }
    fs::create_dir(&exchange_directory).unwrap();

    let mut first = AutoCommit::new().with_actor(actor(0x66));
    first.put(ROOT, "first", "one").unwrap();
    first.commit();
    first.put(ROOT, "second", 2).unwrap();
    first.commit();

    let mut second = AutoCommit::new().with_actor(actor(0x77));
    second.put(ROOT, "remote", true).unwrap();
    second.commit();

    write_sync_document(&exchange_directory.join("first.am"), &mut first);
    write_sync_document(&exchange_directory.join("second.am"), &mut second);

    let mut first_state = State::new();
    let mut second_state = State::new();
    let mut message_number = 0;

    loop {
        let first_message = first.sync().generate_sync_message(&mut first_state);
        let second_message = second.sync().generate_sync_message(&mut second_state);
        if first_message.is_none() && second_message.is_none() {
            break;
        }
        assert!(message_number < 20, "sync exchange did not converge");

        if let Some(message) = first_message {
            let bytes = encode_v1(message);
            let decoded = Message::decode(&bytes).unwrap();
            let filename = format!("{message_number:02}-first-to-second.sync");
            fs::write(exchange_directory.join(filename), bytes).unwrap();
            second
                .sync()
                .receive_sync_message(&mut second_state, decoded)
                .unwrap();
            message_number += 1;
        }

        if let Some(message) = second_message {
            let bytes = encode_v1(message);
            let decoded = Message::decode(&bytes).unwrap();
            let filename = format!("{message_number:02}-second-to-first.sync");
            fs::write(exchange_directory.join(filename), bytes).unwrap();
            first
                .sync()
                .receive_sync_message(&mut first_state, decoded)
                .unwrap();
            message_number += 1;
        }
    }

    assert_eq!(first.get_heads(), second.get_heads());
}

fn main() {
    write_fixture("map_puts", &mut map_puts());
    write_fixture("list_inserts", &mut list_inserts());
    write_fixture("text_splice", &mut text_splice());
    write_fixture("two_actor_merge", &mut two_actor_merge());
    write_incremental_fixture();
    sync_exchange();
}
